// DatabaseFramework.cpp - 核心框架实现文件
#include "DatabaseFramework.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>

#include "BaseDatabaseManager.h"  // 新增：提供 ConnectionPool 的完整定义
#include "DatabaseFramework.h"

// ============================================================================
// DatabaseConfig实现
// ============================================================================

DatabaseConfig DatabaseConfig::fromFile(const QString& configPath) {
  DatabaseConfig config;

  if (configPath.endsWith(".json")) {
    // JSON格式配置
    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly)) {
      QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
      QJsonObject obj = doc.object();

      config.dbName = obj["dbName"].toString();
      config.filePath = obj["filePath"].toString();
      config.maxConnections = obj["maxConnections"].toInt(10);
      config.busyTimeout = obj["busyTimeout"].toInt(5000);
      config.enableWAL = obj["enableWAL"].toBool(true);
      config.enableForeignKeys = obj["enableForeignKeys"].toBool(true);
      config.enableQueryCache = obj["enableQueryCache"].toBool(true);
      config.queryCacheSize = obj["queryCacheSize"].toInt(100);
      config.configSource = configPath;
    }
  } else {
    // INI格式配置
    QSettings settings(configPath, QSettings::IniFormat);
    config.dbName = settings.value("Database/name").toString();
    config.filePath = settings.value("Database/filePath").toString();
    config.maxConnections =
        settings.value("Database/maxConnections", 10).toInt();
    config.busyTimeout = settings.value("Database/busyTimeout", 5000).toInt();
    config.enableWAL = settings.value("Database/enableWAL", true).toBool();
    config.enableQueryCache =
        settings.value("Performance/enableQueryCache", true).toBool();
    config.configSource = configPath;
  }

  return config;
}

DatabaseConfig DatabaseConfig::fromEnvironment(const QString& prefix) {
  DatabaseConfig config;

  config.dbName = qgetenv(QString("%1NAME").arg(prefix).toLocal8Bit());
  config.filePath = qgetenv(QString("%1PATH").arg(prefix).toLocal8Bit());

  bool ok;
  int maxConn =
      QString::fromLocal8Bit(
          qgetenv(QString("%1MAX_CONNECTIONS").arg(prefix).toLocal8Bit()))
          .toInt(&ok);
  if (ok) config.maxConnections = maxConn;

  int timeout =
      QString::fromLocal8Bit(
          qgetenv(QString("%1BUSY_TIMEOUT").arg(prefix).toLocal8Bit()))
          .toInt(&ok);
  if (ok) config.busyTimeout = timeout;

  config.configSource = "Environment:" + prefix;
  return config;
}

DbResult<bool> DatabaseConfig::validate() const {
  if (dbName.isEmpty()) {
    return DbResult<bool>::Error("数据库名称不能为空");
  }

  if (filePath.isEmpty()) {
    return DbResult<bool>::Error("数据库文件路径不能为空");
  }

  if (maxConnections <= 0 || maxConnections > 100) {
    return DbResult<bool>::Error("最大连接数必须在1-100之间");
  }

  if (busyTimeout < 1000) {
    return DbResult<bool>::Error("忙等超时时间不能少于1000ms");
  }

  return DbResult<bool>::Success(true);
}

// ============================================================================
// BaseTableOperations实现
// ============================================================================

BaseTableOperations::BaseTableOperations(QSqlDatabase* db,
                                         const QString& tableName,
                                         TableType tableType,
                                         ConnectionPool* pool, QObject* parent)
    : QObject(parent),
      m_database(db),
      m_tableName(tableName),
      m_tableType(tableType),
      m_pool(pool) {
  logOperation("构造函数", QString("表操作对象已创建: %1").arg(tableName));
}

BaseTableOperations::ScopedDb::~ScopedDb() {
  if (pool && !name.isEmpty()) {
    pool->releaseConnection(name);
  }
}

BaseTableOperations::ScopedDb BaseTableOperations::acquireDb() const {
  if (m_pool) {
    const QString name = m_pool->acquireConnection();
    return ScopedDb{name, QSqlDatabase::database(name), m_pool};
  }
  // 无连接池：直接复制主连接句柄（不负责关闭）
  return ScopedDb{QString(), *m_database, nullptr};
}

bool BaseTableOperations::tableExists() {
  QMutexLocker locker(&m_mutex);
  auto c = acquireDb();
  if (!c.db.isOpen()) return false;

  QSqlQuery query(c.db);
  query.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?");
  query.addBindValue(m_tableName);
  return query.exec() && query.next();
}

int BaseTableOperations::getTotalCount() const {
  QMutexLocker locker(&m_mutex);
  auto c = acquireDb();
  if (!c.db.isOpen()) return 0;

  QSqlQuery query(c.db);
  query.prepare(QString("SELECT COUNT(*) FROM %1").arg(m_tableName));
  return (query.exec() && query.next()) ? query.value(0).toInt() : 0;
}

bool BaseTableOperations::dropTable() {
  QMutexLocker locker(&m_mutex);
  auto c = acquireDb();
  if (!c.db.isOpen()) return false;

  QSqlQuery query(c.db);
  const bool ok =
      query.exec(QString("DROP TABLE IF EXISTS %1").arg(m_tableName));
  logOperation(ok ? "删除表成功" : "删除表失败",
               ok ? m_tableName : query.lastError().text());
  return ok;
}

bool BaseTableOperations::truncateTable() {
  QMutexLocker locker(&m_mutex);
  auto c = acquireDb();
  if (!c.db.isOpen()) return false;

  QSqlQuery query(c.db);
  const bool ok = query.exec(QString("DELETE FROM %1").arg(m_tableName));
  logOperation(ok ? "清空表成功" : "清空表失败",
               ok ? m_tableName : query.lastError().text());
  return ok;
}

bool BaseTableOperations::executeQuery(const QString& sql,
                                       const QVariantList& params) const {
  auto c = acquireDb();
  if (!c.db.isOpen()) {
    qWarning() << "数据库未打开";
    return false;
  }

  QElapsedTimer t;
  t.start();

  QSqlQuery query(c.db);
  query.prepare(sql);
  for (const auto& p : params) query.addBindValue(p);
  const bool ok = query.exec();
  const qint64 ms = t.elapsed();

  if (!ok) {
    qWarning() << QString("SQL执行失败 [%1ms]: %2")
                      .arg(ms)
                      .arg(query.lastError().text());
    qWarning() << "SQL语句:" << sql;
    if (!params.isEmpty()) qWarning() << "参数:" << params;
    return false;
  }

  qDebug() << QString("SQL成功 [%1ms]").arg(ms);
  return true;
}

void BaseTableOperations::logOperation(const QString& operation,
                                       const QString& details) const {
  QString logMessage =
      QString("[%1:%2] %3")
          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"))
          .arg(m_tableName)
          .arg(operation);

  if (!details.isEmpty()) {
    logMessage += QString(" - %1").arg(details);
  }

  qInfo() << logMessage;
}
