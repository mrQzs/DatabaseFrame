// BaseDatabaseManager.cpp - 数据库管理器基类实现
#include "BaseDatabaseManager.h"

#include <QElapsedTimer>
#include <QThread>

// ============================================================================
// 连接池实现
// ============================================================================

void ConnectionPool::cleanupFinishedThreads() {
  // 移除已结束线程的可用连接队列，避免被新线程（指针地址复用）误用
  QList<QString> stale;
  for (auto it = m_threadRefs.begin(); it != m_threadRefs.end(); ++it) {
    if (it.value().isNull()) stale.append(it.key());
  }
  for (const QString& tid : stale) {
    auto& q = m_availableByThread[tid];
    while (!q.isEmpty()) {
      QSqlDatabase::removeDatabase(q.dequeue());
    }
    m_availableByThread.remove(tid);
    m_activeTxByThread.remove(tid);
    m_threadRefs.remove(tid);
  }
}

ConnectionPool::ConnectionPool(const DatabaseConfig& config)
    : m_connectionNamePrefix(config.connectionName), m_config(config) {}

ConnectionPool::~ConnectionPool() {
  QMutexLocker locker(&m_mutex);
  // 先清空可用
  for (auto& q : m_availableByThread) {
    while (!q.isEmpty()) QSqlDatabase::removeDatabase(q.dequeue());
  }
  // 再移除使用中（理论上关闭前应已归还）
  for (const QString& name : m_usedConnections) {
    QSqlDatabase::removeDatabase(name);
  }
  m_usedConnections.clear();
  m_connOwner.clear();
}

QString ConnectionPool::acquireConnection() {
  QMutexLocker locker(&m_mutex);
  const QString tid = currentTid();

  cleanupFinishedThreads();
  m_threadRefs.insert(tid, QThread::currentThread());

  // 若该线程有活动事务，则强制复用该连接
  if (m_activeTxByThread.contains(tid)) {
    const QString name = m_activeTxByThread.value(tid);
    return name;
  }

  auto& q = m_availableByThread[tid];
  if (!q.isEmpty()) {
    QString name = q.dequeue();
    m_usedConnections.insert(name);
    return name;
  }

  // 全局连接总数达上限则失败
  if (totalConnectionsUnsafe() >= m_config.maxConnections) return QString();

  QString name = createConnectionInCurrentThread();  // 在当前线程创建
  if (!name.isEmpty()) {
    m_connOwner.insert(name, tid);
    m_usedConnections.insert(name);
  }
  return name;
}

void ConnectionPool::releaseConnection(const QString& name) {
  QMutexLocker locker(&m_mutex);
  cleanupFinishedThreads();
  if (!m_usedConnections.contains(name)) return;
  // 若该连接正被某线程作为活动事务绑定，则忽略释放
  const QString ownerTid = m_connOwner.value(name, currentTid());
  if (m_activeTxByThread.value(ownerTid) == name) {
    return;  // 事务结束时统一释放
  }
  m_usedConnections.remove(name);
  const QString tid = m_connOwner.value(name, currentTid());
  m_availableByThread[tid].enqueue(name);
}

int ConnectionPool::forceCloseIdleConnections() {
  QMutexLocker locker(&m_mutex);
  int closed = 0;
  for (auto it = m_availableByThread.begin(); it != m_availableByThread.end();
       ++it) {
    auto& q = it.value();
    while (!q.isEmpty()) {
      QSqlDatabase::removeDatabase(q.dequeue());
      ++closed;
    }
  }
  return closed;
}

int ConnectionPool::availableCount() const {
  QMutexLocker locker(&m_mutex);
  int total = 0;
  for (auto it = m_availableByThread.constBegin();
       it != m_availableByThread.constEnd(); ++it) {
    total += it.value().size();
  }
  return total;
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

QString ConnectionPool::createConnectionInCurrentThread() {
  QString threadId =
      QString::number(reinterpret_cast<qintptr>(QThread::currentThread()));
  QString connectionName = QString("%1_%2_%3")
                               .arg(m_config.connectionName)
                               .arg(threadId)
                               .arg(++m_connectionCounter);

  // 在当前线程中创建并打开连接
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
  db.setDatabaseName(m_config.filePath);
  db.setConnectOptions(
      QString("QSQLITE_BUSY_TIMEOUT=%1").arg(m_config.busyTimeout));

  if (!db.open()) {
    qWarning() << "Failed to create database connection in thread"
               << QThread::currentThread() << ":" << db.lastError().text();
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

// ---- 线程事务：开始/提交/回滚 ----
QString ConnectionPool::beginThreadTransaction() {
  QMutexLocker locker(&m_mutex);
  const QString tid = currentTid();
  if (m_activeTxByThread.contains(tid)) {
    return m_activeTxByThread.value(tid);  // 已有事务，复用
  }

  QString name;
  auto& q = m_availableByThread[tid];
  if (!q.isEmpty()) {
    name = q.dequeue();
    m_usedConnections.insert(name);
  } else {
    if (totalConnectionsUnsafe() >= m_config.maxConnections) return QString();
    name = createConnectionInCurrentThread();
    if (!name.isEmpty()) {
      m_connOwner.insert(name, tid);
      m_usedConnections.insert(name);
    }
  }
  if (name.isEmpty()) return QString();

  // 在这条连接上开启事务
  locker.unlock();
  QSqlDatabase db = QSqlDatabase::database(name);
  if (!db.isOpen() || !db.transaction()) {
    locker.relock();
    // 回退：放回可用队列
    m_usedConnections.remove(name);
    m_availableByThread[tid].enqueue(name);
    return QString();
  }
  locker.relock();
  m_activeTxByThread.insert(tid, name);
  return name;
}

bool ConnectionPool::commitThreadTransaction() {
  QString name;
  {
    QMutexLocker locker(&m_mutex);
    const QString tid = currentTid();
    name = m_activeTxByThread.take(tid);
  }
  if (name.isEmpty()) return false;
  QSqlDatabase db = QSqlDatabase::database(name);
  bool ok = db.commit();
  // 提交后归还连接
  releaseConnection(name);
  return ok;
}

bool ConnectionPool::rollbackThreadTransaction() {
  QString name;
  {
    QMutexLocker locker(&m_mutex);
    const QString tid = currentTid();
    name = m_activeTxByThread.take(tid);
  }
  if (name.isEmpty()) return false;
  QSqlDatabase db = QSqlDatabase::database(name);
  bool ok = db.rollback();
  releaseConnection(name);
  return ok;
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
    // 若连接池已在 close() 中释放，则此处重建
    if (!m_connectionPool) {
      m_connectionPool = std::make_unique<ConnectionPool>(m_config);
    }

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
    qInfo() << "数据库表创建阶段完成，开始初始化健康检查...";
    initializeHealthCheck();
    qInfo() << "健康检查初始化完成";

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

  // 先销毁连接池，确保不再持有任何数据库文件句柄（包含 WAL/-shm）
  if (m_connectionPool) {
    m_connectionPool.reset();
  }

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

  m_database = QSqlDatabase();

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
  // 优先使用连接池的“线程事务”，以绑定具体连接
  if (m_connectionPool) {
    const QString name = m_connectionPool->beginThreadTransaction();
    if (name.isEmpty()) {
      qWarning() << "开始线程事务失败";
      return false;
    }
    emit transactionBegin();
    qDebug() << "事务开始（池连接）:" << name;
    return true;
  }
  // 回退到主连接（无连接池）
  if (!m_database.isOpen()) {
    qWarning() << "数据库未打开，无法开始事务";
    return false;
  }
  const bool ok = m_database.transaction();
  if (ok) emit transactionBegin();
  return ok;
}

bool BaseDatabaseManager::commitTransaction() {
  QMutexLocker locker(&m_dbMutex);
  if (m_connectionPool) {
    const bool ok = m_connectionPool->commitThreadTransaction();
    if (!ok) {
      qWarning() << "提交线程事务失败";
      return false;
    }
    emit transactionCommitted();
    qDebug() << "事务提交成功（池连接）";
    return true;
  }
  if (!m_database.isOpen()) {
    qWarning() << "数据库未打开，无法提交事务";
    return false;
  }
  const bool ok = m_database.commit();
  if (ok) emit transactionCommitted();
  return ok;
}

bool BaseDatabaseManager::rollbackTransaction() {
  QMutexLocker locker(&m_dbMutex);
  if (m_connectionPool) {
    const bool ok = m_connectionPool->rollbackThreadTransaction();
    if (!ok) {
      qWarning() << "回滚线程事务失败";
      return false;
    }
    emit transactionRolledBack();
    qDebug() << "事务回滚成功（池连接）";
    return true;
  }
  if (!m_database.isOpen()) {
    qWarning() << "数据库未打开，无法回滚事务";
    return false;
  }
  const bool ok = m_database.rollback();
  if (ok) emit transactionRolledBack();
  return ok;
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

  // 添加调试信息
  qDebug() << "表总数:" << m_tables.size();
  qDebug() << "当前线程ID:" << QThread::currentThreadId();

  int successCount = 0;
  int totalCount = m_tables.size();

  qDebug() << "开始遍历表集合...";

  for (const auto& pair : m_tables) {
    TableType tableType = pair.first;
    const auto& table = pair.second;

    qDebug() << "=== 处理表 ===" << static_cast<int>(tableType)
             << table->tableName();

    try {
      qDebug() << "调用 createTable() 方法...";
      bool result = table->createTable();
      qDebug() << "createTable() 返回结果:" << result;

      if (result) {
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

    qDebug() << "当前进度:" << successCount << "/" << totalCount;
  }

  qDebug() << "for循环结束，准备输出最终日志...";
  qInfo()
      << QString("表创建完成: %1/%2 成功").arg(successCount).arg(totalCount);

  qDebug() << "准备返回结果...";
  bool finalResult = (successCount == totalCount);
  qDebug() << "最终结果:" << finalResult;

  return finalResult;
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

  // 执行简单查询测试连接 + 统计
  QElapsedTimer timer;
  timer.start();
  QSqlQuery query(m_database);
  bool healthy = query.exec("SELECT 1");
  // 记一次统计（无论成功失败都计数，方便观测）
  recordQueryStats(healthy, static_cast<double>(timer.elapsed()));

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

  // 清空空闲池连接；若仍有活跃连接，则直接返回失败，避免 VACUUM 被并发阻塞
  if (m_connectionPool) {
    m_connectionPool->forceCloseIdleConnections();
    if (m_connectionPool->usedCount() > 0) {
      qWarning() << "存在活跃池连接，跳过 VACUUM/ANALYZE";
      return false;
    }
  }
  QSqlQuery query(m_database);
  bool success = true;

  if (m_config.enableWAL) {
    QElapsedTimer t;
    t.start();
    bool walOk = query.exec("PRAGMA wal_checkpoint(TRUNCATE)");
    recordQueryStats(walOk, static_cast<double>(t.elapsed()));
    // walOk 失败不立即置 overall 失败，仅记录日志由下面流程汇总
  }

  {
    QElapsedTimer t;
    t.start();
    bool ok = query.exec("VACUUM");
    recordQueryStats(ok, static_cast<double>(t.elapsed()));
    if (!ok) {
      qWarning() << "VACUUM失败:" << query.lastError().text();
      success = false;
    }
  }

  {
    QElapsedTimer t;
    t.start();
    bool ok = query.exec("ANALYZE");
    recordQueryStats(ok, static_cast<double>(t.elapsed()));
    if (!ok) {
      qWarning() << "ANALYZE失败:" << query.lastError().text();
      success = false;
    }
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

  QElapsedTimer t;
  t.start();
  bool ok = query.exec(sql);
  recordQueryStats(ok, static_cast<double>(t.elapsed()));

  if (ok) {
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
  qint64 total = 0;
  QFileInfo mainFi(m_config.filePath);
  if (mainFi.exists()) total += mainFi.size();
  QFileInfo walFi(m_config.filePath + "-wal");
  if (walFi.exists()) total += walFi.size();
  QFileInfo shmFi(m_config.filePath + "-shm");
  if (shmFi.exists()) total += shmFi.size();
  return total;
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

  QString pooledName;
  QSqlDatabase dbToUse = m_database;
  if (m_connectionPool) {
    pooledName = m_connectionPool->acquireConnection();
    if (!pooledName.isEmpty())
      dbToUse = QSqlDatabase::database(pooledName);
    else {
      double qt = timer.elapsed();
      recordQueryStats(false, qt);
      qWarning() << "统计查询获取池连接失败";
      return false;
    }
  }
  QSqlQuery query(dbToUse);
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

  if (m_connectionPool && !pooledName.isEmpty()) {
    m_connectionPool->releaseConnection(pooledName);
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
