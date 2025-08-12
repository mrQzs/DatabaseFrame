// BaseDatabaseManager.h - 数据库管理器基类
#ifndef BASE_DATABASE_MANAGER_H
#define BASE_DATABASE_MANAGER_H

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutex>
#include <QPointer>
#include <QQueue>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QTimer>
#include <memory>
#include <unordered_map>

#include "DatabaseFramework.h"

/**
 * @brief 连接池类
 * 管理数据库连接池，提供连接的获取和释放功能
 */
class ConnectionPool {
 private:
  QString m_connectionNamePrefix;          ///< 连接名前缀
  DatabaseConfig m_config;                 ///< 数据库配置
  QQueue<QString> m_availableConnections;  ///< 可用连接队列
  QSet<QString> m_usedConnections;         ///< 已使用连接集合
  mutable QMutex m_mutex;                  ///< 线程安全锁
  int m_connectionCounter = 0;             ///< 连接计数器
  QHash<QString, QQueue<QString>> m_availableByThread;  // key: threadId
  QHash<QString, QString> m_connOwner;                  // connName -> threadId
  QHash<QString, QString>
      m_activeTxByThread;  // threadId -> connName  (活动事务绑定)
  QHash<QString, QPointer<QThread>> m_threadRefs;

  static QString currentTid() {
    return QString::number(reinterpret_cast<qintptr>(QThread::currentThread()));
  }

  void cleanupFinishedThreads();

  int totalConnectionsUnsafe() const {
    int n = m_usedConnections.size();
    for (const auto& q : m_availableByThread) n += q.size();
    return n;
  }

 public:
  /**
   * @brief 构造函数
   * @param config 数据库配置
   */
  explicit ConnectionPool(const DatabaseConfig& config);

  /**
   * @brief 析构函数
   */
  ~ConnectionPool();

  /**
   * @brief 获取连接
   * @return 连接名称
   */
  QString acquireConnection();

  /**
   * @brief 释放连接
   * @param connectionName 连接名称
   */
  void releaseConnection(const QString& connectionName);

  // 关闭所有空闲连接，返回关闭数量
  int forceCloseIdleConnections();

  /**
   * @brief 获取可用连接数
   * @return 可用连接数
   */
  int availableCount() const;

  /**
   * @brief 获取已使用连接数
   * @return 已使用连接数
   */
  int usedCount() const;

 private:
  /**
   * @brief 创建新连接
   * @return 连接名称
   */
  QString createConnection();

  QString createConnectionInCurrentThread();

  /**
   * @brief 配置数据库连接
   * @param db 数据库对象
   */
  void configureDatabase(QSqlDatabase& db);

 public:
  // 线程级事务：开始/提交/回滚（绑定当前线程的一条连接）
  QString beginThreadTransaction();  // 返回绑定的连接名（失败则为空）
  bool commitThreadTransaction();    // 提交并释放绑定
  bool rollbackThreadTransaction();  // 回滚并释放绑定
};

/**
 * @brief 数据库管理器基类
 * 提供数据库连接管理、事务处理、表管理等基础功能
 */
class BaseDatabaseManager : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief 数据库统计信息结构体
   */
  struct DatabaseStats {
    qint64 totalQueries = 0;       ///< 总查询次数
    qint64 successfulQueries = 0;  ///< 成功查询次数
    qint64 failedQueries = 0;      ///< 失败查询次数
    QDateTime lastQueryTime;       ///< 最后查询时间
    double avgQueryTime = 0.0;     ///< 平均查询时间(毫秒)
  };

 protected:
  DatabaseType m_databaseType;                       ///< 数据库类型
  DatabaseConfig m_config;                           ///< 数据库配置
  std::unique_ptr<ConnectionPool> m_connectionPool;  ///< 连接池
  QSqlDatabase m_database;                           ///< 主数据库连接
  mutable QMutex m_dbMutex;  ///< 数据库操作互斥锁

  // 表管理
  std::unordered_map<TableType, std::unique_ptr<ITableOperations>>
      m_tables;                ///< 表管理映射
  QTimer* m_healthCheckTimer;  ///< 健康检查定时器

  // 统计信息
  mutable QMutex m_statsMutex;  ///< 统计信息互斥锁
  DatabaseStats m_stats;        ///< 统计信息

 public:
  /**
   * @brief 构造函数
   * @param dbType 数据库类型
   * @param config 数据库配置
   * @param parent 父对象
   */
  explicit BaseDatabaseManager(DatabaseType dbType,
                               const DatabaseConfig& config,
                               QObject* parent = nullptr);

  /**
   * @brief 析构函数
   */
  virtual ~BaseDatabaseManager();

  // ========================================================================
  // 基础数据库操作
  // ========================================================================

  /**
   * @brief 初始化数据库
   * 创建数据库文件、建立连接、创建表结构
   * @return 是否成功
   */
  virtual bool initialize();

  /**
   * @brief 关闭数据库连接
   */
  virtual void close();

  /**
   * @brief 检查数据库是否已打开
   * @return 是否已打开
   */
  bool isOpen() const;

  /**
   * @brief 获取数据库类型
   * @return 数据库类型
   */
  DatabaseType databaseType() const { return m_databaseType; }

  /**
   * @brief 获取数据库配置
   * @return 数据库配置
   */
  const DatabaseConfig& config() const { return m_config; }

  // ========================================================================
  // 事务管理
  // ========================================================================

  /**
   * @brief 开始事务
   * @return 是否成功
   */
  bool beginTransaction();

  /**
   * @brief 提交事务
   * @return 是否成功
   */
  bool commitTransaction();

  /**
   * @brief 回滚事务
   * @return 是否成功
   */
  bool rollbackTransaction();

  /**
   * @brief 自动事务执行器
   * 使用RAII模式自动管理事务
   * @param operation 要执行的操作
   * @return 操作结果
   */
  template <typename T, typename = void>
  struct has_success_field : std::false_type {};

  template <typename T>
  struct has_success_field<T, std::void_t<decltype(std::declval<T>().success)>>
      : std::true_type {};

  template <typename Func>
  auto executeInTransaction(Func&& operation) -> decltype(operation()) {
    using ReturnType = decltype(operation());

    if (!beginTransaction()) {
      if constexpr (std::is_same_v<ReturnType, bool>) {
        return false;
      } else {
        return ReturnType{};
      }
    }

    try {
      auto result = operation();

      if constexpr (std::is_same_v<ReturnType, bool>) {
        result ? commitTransaction() : rollbackTransaction();
      } else if constexpr (has_success_field<ReturnType>::value) {
        result.success ? commitTransaction() : rollbackTransaction();
      } else {
        // 无法判断，保守选择提交（与之前行为一致）
        commitTransaction();
      }

      return result;
    } catch (...) {
      rollbackTransaction();
      throw;
    }
  }
  // ========================================================================
  // 表管理
  // ========================================================================

  /**
   * @brief 注册表
   * @param tableType 表类型
   * @param table 表操作对象
   */
  void registerTable(TableType tableType,
                     std::unique_ptr<ITableOperations> table);

  /**
   * @brief 获取表操作对象
   * @param tableType 表类型
   * @return 表操作对象指针
   */
  ITableOperations* getTable(TableType tableType);

  /**
   * @brief 创建所有表
   * @return 是否成功
   */
  virtual bool createAllTables();

  /**
   * @brief 删除所有表
   * @return 是否成功
   */
  virtual bool dropAllTables();

  // ========================================================================
  // 数据库维护
  // ========================================================================

  /**
   * @brief 执行数据库健康检查
   * @return 健康检查结果
   */
  virtual bool healthCheck();

  /**
   * @brief 优化数据库
   * 执行VACUUM、ANALYZE等优化操作
   * @return 是否成功
   */
  virtual bool optimizeDatabase();

  /**
   * @brief 备份数据库
   * @param backupPath 备份文件路径
   * @return 是否成功
   */
  virtual bool backupDatabase(const QString& backupPath);

  /**
   * @brief 恢复数据库
   * @param backupPath 备份文件路径
   * @return 是否成功
   */
  virtual bool restoreDatabase(const QString& backupPath);

  // ========================================================================
  // 统计信息
  // ========================================================================

  /**
   * @brief 获取数据库统计信息
   * @return 统计信息结构体
   */
  DatabaseStats getStatistics() const;

  /**
   * @brief 重置统计信息
   */
  void resetStatistics();

  /**
   * @brief 获取数据库大小
   * @return 数据库文件大小（字节）
   */
  qint64 getDatabaseSize() const;

 signals:
  /**
   * @brief 数据库初始化完成信号
   * @param success 是否成功
   */
  void databaseInitialized(bool success);

  /**
   * @brief 数据库错误信号
   * @param error 错误信息
   */
  void databaseError(const QString& error);

  /**
   * @brief 事务开始信号
   */
  void transactionBegin();

  /**
   * @brief 事务提交信号
   */
  void transactionCommitted();

  /**
   * @brief 事务回滚信号
   */
  void transactionRolledBack();

  /**
   * @brief 健康检查完成信号
   * @param healthy 是否健康
   */
  void healthCheckCompleted(bool healthy);

 protected:
  /**
   * @brief 子类实现：注册所有表
   * 子类必须重写此方法来注册自己的表
   */
  virtual void registerTables() = 0;

  /**
   * @brief 创建数据库目录
   * @return 是否成功
   */
  bool createDatabaseDirectory();

  /**
   * @brief 配置数据库连接
   * 设置WAL模式、外键约束等
   * @return 是否成功
   */
  bool configureDatabaseConnection();

  /**
   * @brief 执行初始化SQL语句
   * @return 是否成功
   */
  bool executeInitSql();

  /**
   * @brief 记录查询统计
   * @param success 是否成功
   * @param queryTime 查询耗时（毫秒）
   */
  void recordQueryStats(bool success, double queryTime);

  /**
   * @brief 执行SQL查询并记录统计
   * @param queryStr SQL语句
   * @param params 参数列表
   * @return 是否成功
   */
  bool executeQueryWithStats(const QString& queryStr,
                             const QVariantList& params = QVariantList());

 private slots:
  /**
   * @brief 定时健康检查槽函数
   */
  void performHealthCheck();

 private:
  /**
   * @brief 初始化健康检查定时器
   */
  void initializeHealthCheck();
};

#endif  // BASE_DATABASE_MANAGER_H
