// ============================================================================
// DatabaseRegistry.cpp - 实现文件
// ============================================================================
#include "DatabaseRegistry.h"

// 静态成员初始化
std::unique_ptr<DatabaseRegistry> DatabaseRegistry::s_instance = nullptr;
QMutex DatabaseRegistry::s_instanceMutex;

DatabaseRegistry::DatabaseRegistry(QObject* parent) : QObject(parent) {
  qInfo() << "创建数据库注册中心";

  // 设置默认数据路径
  m_baseDataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (m_baseDataPath.isEmpty()) {
    m_baseDataPath = QDir::currentPath() + "/data";
  }

  qInfo() << "数据库基础路径:" << m_baseDataPath;
}

DatabaseRegistry::~DatabaseRegistry() {
  shutdown();
  qInfo() << "销毁数据库注册中心";
}

DatabaseRegistry* DatabaseRegistry::getInstance() {
  QMutexLocker locker(&s_instanceMutex);
  if (!s_instance) {
    s_instance = std::unique_ptr<DatabaseRegistry>(new DatabaseRegistry());
  }
  return s_instance.get();
}

void DatabaseRegistry::destroyInstance() {
  QMutexLocker locker(&s_instanceMutex);
  s_instance.reset();
}

bool DatabaseRegistry::initialize(const QString& dataPath) {
  QMutexLocker locker(&m_registryMutex);

  if (m_initialized) {
    qWarning() << "数据库注册中心已经初始化";
    return true;
  }

  qInfo() << "初始化数据库注册中心...";

  // 设置数据路径
  if (!dataPath.isEmpty()) {
    m_baseDataPath = dataPath;
  }

  // 确保数据目录存在
  if (!ensureDataDirectoryExists()) {
    QString error = "创建数据目录失败: " + m_baseDataPath;
    qCritical() << error;
    emit initializationCompleted(false, error);
    return false;
  }

  // 注册所有数据库
  QStringList errors;
  int successCount = 0;

  // 注册设备管理数据库
  if (registerDatabase(DatabaseType::DEVICE_DB)) {
    successCount++;
  } else {
    errors.append("设备管理数据库注册失败");
  }

  // 后续可以注册其他数据库
  // if (registerDatabase(DatabaseType::CONFIG_DB)) {
  //     successCount++;
  // } else {
  //     errors.append("用户配置数据库注册失败");
  // }

  // 检查结果
  bool success = (successCount > 0);
  m_initialized = success;

  QString message;
  if (success) {
    message = QString("数据库注册中心初始化完成，成功注册 %1 个数据库")
                  .arg(successCount);
    if (!errors.isEmpty()) {
      message +=
          QString("，%1 个失败: %2").arg(errors.size()).arg(errors.join(", "));
    }
    qInfo() << message;
  } else {
    message = QString("数据库注册中心初始化失败: %1").arg(errors.join(", "));
    qCritical() << message;
  }

  emit initializationCompleted(success, message);
  return success;
}

void DatabaseRegistry::shutdown() {
  QMutexLocker locker(&m_registryMutex);

  if (!m_initialized) {
    return;
  }

  qInfo() << "关闭数据库注册中心...";

  // 关闭所有数据库连接
  for (auto& pair : m_databases) {
    if (pair.second) {
      pair.second->close();
    }
  }

  // 清空数据库映射
  m_databases.clear();

  m_initialized = false;
  qInfo() << "数据库注册中心已关闭";
}

DeviceDatabaseManager* DatabaseRegistry::deviceDatabase() const {
  return static_cast<DeviceDatabaseManager*>(
      getDatabase(DatabaseType::DEVICE_DB));
}

BaseDatabaseManager* DatabaseRegistry::getDatabase(DatabaseType dbType) const {
  QMutexLocker locker(&m_registryMutex);

  auto it = m_databases.find(dbType);
  return (it != m_databases.end()) ? it->second.get() : nullptr;
}

bool DatabaseRegistry::isDatabaseAvailable(DatabaseType dbType) const {
  QMutexLocker locker(&m_registryMutex);

  auto it = m_databases.find(dbType);
  if (it == m_databases.end()) {
    return false;
  }

  return it->second && it->second->isOpen();
}

int DatabaseRegistry::createAllDatabases() {
  QMutexLocker locker(&m_registryMutex);

  int successCount = 0;

  for (auto& pair : m_databases) {
    DatabaseType dbType = pair.first;
    auto& database = pair.second;

    if (database && database->createAllTables()) {
      successCount++;
      qInfo()
          << QString("创建数据库表成功: %1").arg(getDatabaseTypeName(dbType));
    } else {
      qWarning()
          << QString("创建数据库表失败: %1").arg(getDatabaseTypeName(dbType));
    }
  }

  qInfo() << QString("数据库表创建完成: %1/%2 成功")
                 .arg(successCount)
                 .arg(m_databases.size());
  return successCount;
}

DbResult<int> DatabaseRegistry::backupAllDatabases(const QString& backupDir) {
  QMutexLocker locker(&m_registryMutex);

  // 确保备份目录存在
  QDir dir(backupDir);
  if (!dir.exists() && !dir.mkpath(".")) {
    return DbResult<int>::Error("创建备份目录失败: " + backupDir);
  }

  int successCount = 0;
  QStringList errors;

  QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");

  for (const auto& pair : m_databases) {
    DatabaseType dbType = pair.first;
    const auto& database = pair.second;

    if (!database || !database->isOpen()) {
      continue;
    }

    QString backupFileName =
        QString("%1_%2.db").arg(getDatabaseTypeName(dbType)).arg(timestamp);
    QString backupPath = QDir(backupDir).absoluteFilePath(backupFileName);

    if (database->backupDatabase(backupPath)) {
      successCount++;
      qInfo() << QString("备份数据库成功: %1 -> %2")
                     .arg(getDatabaseTypeName(dbType))
                     .arg(backupPath);
    } else {
      QString error =
          QString("备份数据库失败: %1").arg(getDatabaseTypeName(dbType));
      errors.append(error);
      qWarning() << error;
    }
  }

  if (successCount > 0) {
    QString message = QString("成功备份 %1 个数据库").arg(successCount);
    if (!errors.isEmpty()) {
      message += QString("，%1 个失败").arg(errors.size());
    }
    return DbResult<int>::Success(successCount);
  } else {
    return DbResult<int>::Error("备份失败: " + errors.join("; "));
  }
}

DbResult<int> DatabaseRegistry::restoreAllDatabases(const QString& backupDir) {
  QMutexLocker locker(&m_registryMutex);

  QDir dir(backupDir);
  if (!dir.exists()) {
    return DbResult<int>::Error("备份目录不存在: " + backupDir);
  }

  int successCount = 0;
  QStringList errors;

  for (const auto& pair : m_databases) {
    DatabaseType dbType = pair.first;
    const auto& database = pair.second;

    if (!database) {
      continue;
    }

    QString dbTypeName = getDatabaseTypeName(dbType);

    // 查找最新的备份文件
    QStringList filters;
    filters << QString("%1_*.db").arg(dbTypeName);
    QFileInfoList backupFiles =
        dir.entryInfoList(filters, QDir::Files, QDir::Time);

    if (backupFiles.isEmpty()) {
      QString error = QString("未找到数据库备份文件: %1").arg(dbTypeName);
      errors.append(error);
      qWarning() << error;
      continue;
    }

    QString latestBackupPath = backupFiles.first().absoluteFilePath();

    if (database->restoreDatabase(latestBackupPath)) {
      successCount++;
      qInfo() << QString("恢复数据库成功: %1 <- %2")
                     .arg(dbTypeName)
                     .arg(latestBackupPath);
    } else {
      QString error = QString("恢复数据库失败: %1").arg(dbTypeName);
      errors.append(error);
      qWarning() << error;
    }
  }

  if (successCount > 0) {
    QString message = QString("成功恢复 %1 个数据库").arg(successCount);
    if (!errors.isEmpty()) {
      message += QString("，%1 个失败").arg(errors.size());
    }
    return DbResult<int>::Success(successCount);
  } else {
    return DbResult<int>::Error("恢复失败: " + errors.join("; "));
  }
}

QMap<DatabaseType, bool> DatabaseRegistry::getDatabaseHealthStatus() const {
  QMutexLocker locker(&m_registryMutex);

  QMap<DatabaseType, bool> healthStatus;

  for (const auto& pair : m_databases) {
    DatabaseType dbType = pair.first;
    const auto& database = pair.second;

    bool healthy = database && database->isOpen() && database->healthCheck();
    healthStatus[dbType] = healthy;
  }

  return healthStatus;
}

QMap<DatabaseType, BaseDatabaseManager::DatabaseStats>
DatabaseRegistry::getAllDatabaseStats() const {
  QMutexLocker locker(&m_registryMutex);

  QMap<DatabaseType, BaseDatabaseManager::DatabaseStats> allStats;

  for (const auto& pair : m_databases) {
    DatabaseType dbType = pair.first;
    const auto& database = pair.second;

    if (database && database->isOpen()) {
      allStats[dbType] = database->getStatistics();
    }
  }

  return allStats;
}

DbResult<int> DatabaseRegistry::optimizeAllDatabases() {
  QMutexLocker locker(&m_registryMutex);

  int successCount = 0;
  QStringList errors;

  for (const auto& pair : m_databases) {
    DatabaseType dbType = pair.first;
    const auto& database = pair.second;

    if (!database || !database->isOpen()) {
      continue;
    }

    if (database->optimizeDatabase()) {
      successCount++;
      qInfo() << QString("优化数据库成功: %1").arg(getDatabaseTypeName(dbType));
    } else {
      QString error =
          QString("优化数据库失败: %1").arg(getDatabaseTypeName(dbType));
      errors.append(error);
      qWarning() << error;
    }
  }

  if (successCount > 0) {
    QString message = QString("成功优化 %1 个数据库").arg(successCount);
    if (!errors.isEmpty()) {
      message += QString("，%1 个失败").arg(errors.size());
    }
    return DbResult<int>::Success(successCount);
  } else {
    return DbResult<int>::Error("优化失败: " + errors.join("; "));
  }
}

DatabaseConfig DatabaseRegistry::getDefaultConfig(DatabaseType dbType) const {
  return createDatabaseConfig(dbType);
}

bool DatabaseRegistry::registerDatabase(DatabaseType dbType) {
  try {
    DatabaseConfig config = createDatabaseConfig(dbType);
    std::unique_ptr<BaseDatabaseManager> database;

    switch (dbType) {
      case DatabaseType::DEVICE_DB:
        database = std::make_unique<DeviceDatabaseManager>(config, this);
        break;

        // 后续可以添加其他数据库类型
        // case DatabaseType::CONFIG_DB:
        //     database = std::make_unique<ConfigDatabaseManager>(config, this);
        //     break;

      default:
        qWarning() << "不支持的数据库类型:" << static_cast<int>(dbType);
        return false;
    }

    if (!database) {
      qWarning() << "创建数据库管理器失败:" << getDatabaseTypeName(dbType);
      return false;
    }

    // 连接信号槽
    connectDatabaseSignals(database.get(), dbType);

    // 初始化数据库
    if (!database->initialize()) {
      qWarning() << "初始化数据库失败:" << getDatabaseTypeName(dbType);
      return false;
    }

    // 注册到映射表
    m_databases[dbType] = std::move(database);

    qInfo() << "数据库注册成功:" << getDatabaseTypeName(dbType);
    emit databaseConnectionChanged(dbType, true);

    return true;

  } catch (const std::exception& e) {
    qCritical() << "注册数据库异常:" << getDatabaseTypeName(dbType) << "-"
                << e.what();
    return false;
  }
}

DatabaseConfig DatabaseRegistry::createDatabaseConfig(
    DatabaseType dbType) const {
  QString dbName = getDatabaseTypeName(dbType);
  QString dbFileName = dbName.toLower() + ".db";
  QString dbPath = QDir(m_baseDataPath).absoluteFilePath(dbFileName);

  DatabaseConfig config(dbName, dbPath);

  // 根据数据库类型设置特定配置
  switch (dbType) {
    case DatabaseType::DEVICE_DB:
      config.maxConnections = 15;
      config.busyTimeout = 10000;  // 设备数据库可能有更多并发操作
      break;

    case DatabaseType::CONFIG_DB:
      config.maxConnections = 8;
      config.busyTimeout = 5000;
      break;

    case DatabaseType::DATA_DB:
      config.maxConnections = 20;  // 数据库可能需要更多连接
      config.busyTimeout = 15000;
      break;

    case DatabaseType::EXPERIMENT_DB:
      config.maxConnections = 12;
      config.busyTimeout = 8000;
      break;

    case DatabaseType::SYSTEM_DB:
      config.maxConnections = 5;
      config.busyTimeout = 3000;
      break;
  }

  return config;
}

QString DatabaseRegistry::getDatabaseTypeName(DatabaseType dbType) const {
  switch (dbType) {
    case DatabaseType::DEVICE_DB:
      return "DeviceDB";
    case DatabaseType::CONFIG_DB:
      return "ConfigDB";
    case DatabaseType::DATA_DB:
      return "DataDB";
    case DatabaseType::EXPERIMENT_DB:
      return "ExperimentDB";
    case DatabaseType::SYSTEM_DB:
      return "SystemDB";
    default:
      return "UnknownDB";
  }
}

bool DatabaseRegistry::ensureDataDirectoryExists() {
  QDir dir(m_baseDataPath);
  if (!dir.exists()) {
    if (!dir.mkpath(".")) {
      qCritical() << "创建数据目录失败:" << m_baseDataPath;
      return false;
    }
    qInfo() << "创建数据目录:" << m_baseDataPath;
  }
  return true;
}

void DatabaseRegistry::connectDatabaseSignals(BaseDatabaseManager* database,
                                              DatabaseType dbType) {
  if (!database) return;

  // 连接数据库初始化信号
  connect(database, &BaseDatabaseManager::databaseInitialized, this,
          &DatabaseRegistry::onDatabaseInitialized);

  // 连接数据库错误信号
  connect(database, &BaseDatabaseManager::databaseError,
          [this, dbType](const QString& error) {
            emit databaseError(dbType, error);
          });

  // 连接健康检查信号
  connect(database, &BaseDatabaseManager::healthCheckCompleted, this,
          &DatabaseRegistry::onHealthCheckCompleted);
}

void DatabaseRegistry::onDatabaseInitialized(bool success) {
  BaseDatabaseManager* database = qobject_cast<BaseDatabaseManager*>(sender());
  if (database) {
    qInfo() << QString("数据库初始化完成: %1 - %2")
                   .arg(database->config().dbName)
                   .arg(success ? "成功" : "失败");
  }
}

void DatabaseRegistry::onDatabaseError(const QString& error) {
  BaseDatabaseManager* database = qobject_cast<BaseDatabaseManager*>(sender());
  if (database) {
    qWarning() << QString("数据库错误 [%1]: %2")
                      .arg(database->config().dbName)
                      .arg(error);
  }
}

void DatabaseRegistry::onHealthCheckCompleted(bool healthy) {
  BaseDatabaseManager* database = qobject_cast<BaseDatabaseManager*>(sender());
  if (database) {
    qDebug() << QString("数据库健康检查 [%1]: %2")
                    .arg(database->config().dbName)
                    .arg(healthy ? "健康" : "异常");
  }

  // 触发全局健康检查
  auto healthStatus = getDatabaseHealthStatus();
  emit healthCheckCompleted(healthStatus);
}
