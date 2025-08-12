// BaseDatabaseManager.cpp - 数据库管理器基类实现
#include "BaseDatabaseManager.h"

#include <QElapsedTimer>

// ============================================================================
// 连接池实现
// ============================================================================

ConnectionPool::ConnectionPool(const DatabaseConfig& config)
    : m_connectionNamePrefix(config.connectionName), m_config(config) {
  // 预创建一些连接
  for (int i = 0; i < qMin(3, config.maxConnections); ++i) {
    QString connName = createConnection();
    if (!connName.isEmpty()) {
      m_availableConnections.enqueue(connName);
    }
  }
}

ConnectionPool::~ConnectionPool() {
  QMutexLocker locker(&m_mutex);

  // 关闭所有连接
  while (!m_availableConnections.isEmpty()) {
    QString connName = m_availableConnections.dequeue();
    QSqlDatabase::removeDatabase(connName);
  }

  for (const QString& connName : m_usedConnections) {
    QSqlDatabase::removeDatabase(connName);
  }
}

QString ConnectionPool::acquireConnection() {
  QMutexLocker locker(&m_mutex);

  QString connectionName;

  if (!m_availableConnections.isEmpty()) {
    // 使用现有连接
    connectionName = m_availableConnections.dequeue();
  } else if (m_usedConnections.size() < m_config.maxConnections) {
    // 创建新连接
    connectionName = createConnection();
  }

  if (!connectionName.isEmpty()) {
    m_usedConnections.insert(connectionName);
  }

  return connectionName;
}

void ConnectionPool::releaseConnection(const QString& connectionName) {
  QMutexLocker locker(&m_mutex);

  if (m_usedConnections.contains(connectionName)) {
    m_usedConnections.remove(connectionName);
    m_availableConnections.enqueue(connectionName);
  }
}

int ConnectionPool::availableCount() const {
  QMutexLocker locker(&m_mutex);
  return m_availableConnections.size();
}

int ConnectionPool::usedCount() const {
  QMutexLocker locker(&m_mutex);
  return m_usedConnections.size();
}

QString ConnectionPool::createConnection() {
  QString connectionName =
      QString("%1_%2").arg(m_connectionNamePrefix).arg(++m_connectionCounter);

  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
  db.setDatabaseName(m_config.filePath);

  // 设置忙等超时
  db.setConnectOptions(
      QString("QSQLITE_BUSY_TIMEOUT=%1").arg(m_config.busyTimeout));

  if (!db.open()) {
    qWarning() << "Failed to create database connection:"
               << db.lastError().text();
    QSqlDatabase::removeDatabase(connectionName);
    return QString();
  }

  configureDatabase(db);
  return connectionName;
}

void ConnectionPool::configureDatabase(QSqlDatabase& db) {
  QSqlQuery query(db);

  // 启用外键约束
  if (m_config.enableForeignKeys) {
    query.exec("PRAGMA foreign_keys = ON");
  }

  // 设置WAL模式
  if (m_config.enableWAL) {
    query.exec("PRAGMA journal_mode = WAL");
  }

  // 设置同步模式为NORMAL以提高性能
  query.exec("PRAGMA synchronous = NORMAL");

  // 设置缓存大小
  query.exec("PRAGMA cache_size = 10000");
}

// ============================================================================
// BaseDatabaseManager实现
// ============================================================================

BaseDatabaseManager::BaseDatabaseManager(DatabaseType dbType,
                                         const DatabaseConfig& config,
                                         QObject* parent)
    : QObject(parent),
      m_databaseType(dbType),
      m_config(config),
      m_healthCheckTimer(nullptr) {
  // 初始化连接池
  m_connectionPool = std::make_unique<ConnectionPool>(config);

  // 初始化统计信息
  m_stats.lastQueryTime = QDateTime::currentDateTime();

  qInfo() << QString("创建数据库管理器 [%1]: %2")
                 .arg(static_cast<int>(dbType))
                 .arg(config.dbName);
}

BaseDatabaseManager::~BaseDatabaseManager() {
  close();
  qInfo() << QString("销毁数据库管理器 [%1]").arg(m_config.dbName);
}

bool BaseDatabaseManager::initialize() {
  QMutexLocker locker(&m_dbMutex);

  qInfo() << QString("初始化数据库 [%1]: %2")
                 .arg(m_config.dbName)
                 .arg(m_config.filePath);

  try {
    // 创建数据库目录
    if (!createDatabaseDirectory()) {
      emit databaseError("创建数据库目录失败");
      return false;
    }

    // 建立主连接
    m_database = QSqlDatabase::addDatabase("QSQLITE", m_config.connectionName);

    m_database.setConnectOptions(
        QString("QSQLITE_BUSY_TIMEOUT=%1").arg(m_config.busyTimeout));

    m_database.setDatabaseName(m_config.filePath);

    if (!m_database.open()) {
      QString error =
          QString("打开数据库失败: %1").arg(m_database.lastError().text());
      qCritical() << error;
      emit databaseError(error);
      return false;
    }

    // 配置数据库连接
    if (!configureDatabaseConnection()) {
      emit databaseError("配置数据库连接失败");
      return false;
    }

    // 执行初始化SQL
    if (!executeInitSql()) {
      emit databaseError("执行初始化SQL失败");
      return false;
    }

    // 注册表
    registerTables();

    // 创建所有表
    if (!createAllTables()) {
      emit databaseError("创建数据表失败");
      return false;
    }

    // 初始化健康检查
    initializeHealthCheck();

    qInfo() << QString("数据库初始化完成 [%1]").arg(m_config.dbName);
    emit databaseInitialized(true);
    return true;

  } catch (const std::exception& e) {
    QString error = QString("数据库初始化异常: %1").arg(e.what());
    qCritical() << error;
    emit databaseError(error);
    emit databaseInitialized(false);
    return false;
  }
}

void BaseDatabaseManager::close() {
  QMutexLocker locker(&m_dbMutex);

  // 停止健康检查
  if (m_healthCheckTimer) {
    m_healthCheckTimer->stop();
    m_healthCheckTimer->deleteLater();  // 使用 deleteLater 而不是 delete
    m_healthCheckTimer = nullptr;
  }

  // 清理表对象
  m_tables.clear();

  // 确保所有查询都已完成
  if (m_database.isOpen()) {
    // 等待所有活跃查询完成
    QSqlQuery query(m_database);
    query.clear();
  }

  // 关闭主连接
  QString connectionName = m_config.connectionName;
  if (m_database.isOpen()) {
    m_database.close();
  }

  // 移除数据库连接
  if (QSqlDatabase::contains(connectionName)) {
    QSqlDatabase::removeDatabase(connectionName);
  }

  qInfo() << QString("数据库连接已关闭 [%1]").arg(m_config.dbName);
}

bool BaseDatabaseManager::isOpen() const {
  QMutexLocker locker(&m_dbMutex);
  return m_database.isOpen();
}

bool BaseDatabaseManager::beginTransaction() {
  QMutexLocker locker(&m_dbMutex);

  if (!m_database.isOpen()) {
    qWarning() << "数据库未打开，无法开始事务";
    return false;
  }

  bool success = m_database.transaction();
  if (success) {
    emit transactionBegin();
    qDebug() << "事务开始";
  } else {
    qWarning() << "开始事务失败:" << m_database.lastError().text();
  }

  return success;
}

bool BaseDatabaseManager::commitTransaction() {
  QMutexLocker locker(&m_dbMutex);

  if (!m_database.isOpen()) {
    qWarning() << "数据库未打开，无法提交事务";
    return false;
  }

  bool success = m_database.commit();
  if (success) {
    emit transactionCommitted();
    qDebug() << "事务提交成功";
  } else {
    qWarning() << "提交事务失败:" << m_database.lastError().text();
  }

  return success;
}

bool BaseDatabaseManager::rollbackTransaction() {
  QMutexLocker locker(&m_dbMutex);

  if (!m_database.isOpen()) {
    qWarning() << "数据库未打开，无法回滚事务";
    return false;
  }

  bool success = m_database.rollback();
  if (success) {
    emit transactionRolledBack();
    qDebug() << "事务回滚成功";
  } else {
    qWarning() << "回滚事务失败:" << m_database.lastError().text();
  }

  return success;
}

void BaseDatabaseManager::registerTable(
    TableType tableType, std::unique_ptr<ITableOperations> table) {
  m_tables[tableType] = std::move(table);
  qDebug() << QString("注册表 [%1]: %2")
                  .arg(static_cast<int>(tableType))
                  .arg(m_tables[tableType]->tableName());
}

ITableOperations* BaseDatabaseManager::getTable(TableType tableType) {
  auto it = m_tables.find(tableType);
  return (it != m_tables.end()) ? it->second.get() : nullptr;
}

bool BaseDatabaseManager::createAllTables() {
  qInfo() << QString("开始创建所有数据表 [%1]").arg(m_config.dbName);

  int successCount = 0;
  int totalCount = m_tables.size();

  for (const auto& pair : m_tables) {
    TableType tableType = pair.first;
    const auto& table = pair.second;

    try {
      if (table->createTable()) {
        successCount++;
        qInfo() << QString("创建表成功: %1").arg(table->tableName());
      } else {
        qWarning() << QString("创建表失败: %1").arg(table->tableName());
      }
    } catch (const std::exception& e) {
      qCritical() << QString("创建表异常 [%1]: %2")
                         .arg(table->tableName())
                         .arg(e.what());
    }
  }

  qInfo()
      << QString("表创建完成: %1/%2 成功").arg(successCount).arg(totalCount);
  return successCount == totalCount;
}

bool BaseDatabaseManager::dropAllTables() {
  qInfo() << QString("开始删除所有数据表 [%1]").arg(m_config.dbName);

  int successCount = 0;
  int totalCount = m_tables.size();

  for (const auto& pair : m_tables) {
    const auto& table = pair.second;

    try {
      if (table->dropTable()) {
        successCount++;
        qInfo() << QString("删除表成功: %1").arg(table->tableName());
      } else {
        qWarning() << QString("删除表失败: %1").arg(table->tableName());
      }
    } catch (const std::exception& e) {
      qCritical() << QString("删除表异常 [%1]: %2")
                         .arg(table->tableName())
                         .arg(e.what());
    }
  }

  qInfo()
      << QString("表删除完成: %1/%2 成功").arg(successCount).arg(totalCount);
  return successCount == totalCount;
}

bool BaseDatabaseManager::healthCheck() {
  QMutexLocker locker(&m_dbMutex);

  if (!m_database.isOpen()) {
    return false;
  }

  // 执行简单查询测试连接
  QSqlQuery query(m_database);
  bool healthy = query.exec("SELECT 1");

  if (!healthy) {
    qWarning() << QString("数据库健康检查失败 [%1]: %2")
                      .arg(m_config.dbName)
                      .arg(query.lastError().text());
  }

  return healthy;
}

bool BaseDatabaseManager::optimizeDatabase() {
  QMutexLocker locker(&m_dbMutex);

  if (!m_database.isOpen()) {
    return false;
  }

  qInfo() << QString("开始优化数据库 [%1]").arg(m_config.dbName);

  QSqlQuery query(m_database);
  bool success = true;

  // 执行VACUUM
  if (!query.exec("VACUUM")) {
    qWarning() << "VACUUM失败:" << query.lastError().text();
    success = false;
  }

  // 执行ANALYZE
  if (!query.exec("ANALYZE")) {
    qWarning() << "ANALYZE失败:" << query.lastError().text();
    success = false;
  }

  qInfo() << QString("数据库优化完成 [%1]: %2")
                 .arg(m_config.dbName)
                 .arg(success ? "成功" : "失败");
  return success;
}

bool BaseDatabaseManager::backupDatabase(const QString& backupPath) {
  QMutexLocker locker(&m_dbMutex);

  if (!m_database.isOpen()) {
    return false;
  }

  qInfo() << QString("开始备份数据库 [%1] 到: %2")
                 .arg(m_config.dbName)
                 .arg(backupPath);

  // 确保备份目录存在
  QFileInfo backupFileInfo(backupPath);
  QDir backupDir = backupFileInfo.absoluteDir();
  if (!backupDir.exists()) {
    if (!backupDir.mkpath(".")) {
      qWarning() << "创建备份目录失败:" << backupDir.absolutePath();
      return false;
    }
  }

  // 使用SQLite的BACKUP API进行在线备份
  QSqlQuery query(m_database);
  QString sql = QString("VACUUM INTO '%1'").arg(backupPath);

  if (query.exec(sql)) {
    qInfo() << QString("数据库备份完成 [%1]").arg(m_config.dbName);
    return true;
  } else {
    qWarning() << QString("数据库备份失败 [%1]: %2")
                      .arg(m_config.dbName)
                      .arg(query.lastError().text());
    return false;
  }
}

bool BaseDatabaseManager::restoreDatabase(const QString& backupPath) {
  if (!QFile::exists(backupPath)) {
    qWarning() << "备份文件不存在:" << backupPath;
    return false;
  }

  qInfo() << QString("开始恢复数据库 [%1] 从: %2")
                 .arg(m_config.dbName)
                 .arg(backupPath);

  // 关闭当前连接
  close();

  // 替换数据库文件
  if (QFile::exists(m_config.filePath)) {
    if (!QFile::remove(m_config.filePath)) {
      qWarning() << "删除旧数据库文件失败:" << m_config.filePath;
      return false;
    }
  }

  if (!QFile::copy(backupPath, m_config.filePath)) {
    qWarning() << "复制备份文件失败";
    return false;
  }

  // 重新初始化
  bool success = initialize();
  qInfo() << QString("数据库恢复完成 [%1]: %2")
                 .arg(m_config.dbName)
                 .arg(success ? "成功" : "失败");

  return success;
}

BaseDatabaseManager::DatabaseStats BaseDatabaseManager::getStatistics() const {
  QMutexLocker locker(&m_statsMutex);
  return m_stats;
}

void BaseDatabaseManager::resetStatistics() {
  QMutexLocker locker(&m_statsMutex);
  m_stats = DatabaseStats{};
  m_stats.lastQueryTime = QDateTime::currentDateTime();
}

qint64 BaseDatabaseManager::getDatabaseSize() const {
  QFileInfo fileInfo(m_config.filePath);
  return fileInfo.exists() ? fileInfo.size() : 0;
}

bool BaseDatabaseManager::createDatabaseDirectory() {
  QFileInfo fileInfo(m_config.filePath);
  QDir dir = fileInfo.absoluteDir();

  if (!dir.exists()) {
    if (!dir.mkpath(".")) {
      qCritical() << "创建数据库目录失败:" << dir.absolutePath();
      return false;
    }
    qInfo() << "创建数据库目录:" << dir.absolutePath();
  }

  return true;
}

bool BaseDatabaseManager::configureDatabaseConnection() {
  QSqlQuery query(m_database);

  // 启用外键约束
  if (m_config.enableForeignKeys) {
    if (!query.exec("PRAGMA foreign_keys = ON")) {
      qWarning() << "启用外键约束失败:" << query.lastError().text();
      return false;
    }
  }

  // 设置WAL模式
  if (m_config.enableWAL) {
    if (!query.exec("PRAGMA journal_mode = WAL")) {
      qWarning() << "设置WAL模式失败:" << query.lastError().text();
      return false;
    }
  }

  // 其他优化设置
  query.exec(QString("PRAGMA busy_timeout = %1").arg(m_config.busyTimeout));
  query.exec("PRAGMA synchronous = NORMAL");
  query.exec("PRAGMA cache_size = 10000");
  query.exec("PRAGMA temp_store = MEMORY");
  // 可选：确保触发器不会递归
  query.exec("PRAGMA recursive_triggers = OFF");

  return true;
}

bool BaseDatabaseManager::executeInitSql() {
  if (m_config.initSqlList.isEmpty()) {
    return true;
  }

  QSqlQuery query(m_database);

  for (const QString& sql : m_config.initSqlList) {
    if (!sql.trimmed().isEmpty()) {
      if (!query.exec(sql)) {
        qWarning() << QString("执行初始化SQL失败 [%1]: %2")
                          .arg(sql)
                          .arg(query.lastError().text());
        return false;
      }
    }
  }

  return true;
}

void BaseDatabaseManager::recordQueryStats(bool success, double queryTime) {
  QMutexLocker locker(&m_statsMutex);

  m_stats.totalQueries++;
  if (success) {
    m_stats.successfulQueries++;
  } else {
    m_stats.failedQueries++;
  }

  m_stats.lastQueryTime = QDateTime::currentDateTime();

  // 计算平均查询时间
  m_stats.avgQueryTime =
      (m_stats.avgQueryTime * (m_stats.totalQueries - 1) + queryTime) /
      m_stats.totalQueries;
}

bool BaseDatabaseManager::executeQueryWithStats(const QString& queryStr,
                                                const QVariantList& params) {
  QElapsedTimer timer;
  timer.start();

  QSqlQuery query(m_database);
  query.prepare(queryStr);

  for (const auto& param : params) {
    query.addBindValue(param);
  }

  bool success = query.exec();
  double queryTime = timer.elapsed();

  recordQueryStats(success, queryTime);

  if (!success) {
    qWarning() << "SQL执行失败:" << query.lastError().text();
    qWarning() << "SQL语句:" << queryStr;
  }

  return success;
}

void BaseDatabaseManager::performHealthCheck() {
  bool healthy = healthCheck();
  emit healthCheckCompleted(healthy);

  if (!healthy) {
    qWarning() << QString("数据库健康检查失败 [%1]").arg(m_config.dbName);
  }
}

void BaseDatabaseManager::initializeHealthCheck() {
  if (m_healthCheckTimer) {
    m_healthCheckTimer->deleteLater();
    m_healthCheckTimer = nullptr;
  }

  m_healthCheckTimer = new QTimer(this);
  connect(m_healthCheckTimer, &QTimer::timeout, this,
          &BaseDatabaseManager::performHealthCheck);

  // 每5分钟执行一次健康检查
  m_healthCheckTimer->start(5 * 60 * 1000);
}
