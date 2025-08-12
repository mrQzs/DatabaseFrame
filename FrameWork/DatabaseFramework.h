// DatabaseFramework.h - 核心框架头文件
#ifndef DATABASE_FRAMEWORK_H
#define DATABASE_FRAMEWORK_H

// 只保留必要的Qt核心头文件
#include <QDateTime>
#include <QDebug>
#include <QMutex>
#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QUuid>
#include <QVariant>
#include <memory>
#include <unordered_map>

// 使用前向声明替代包含
class QSqlQuery;
class QSqlError;
class ConnectionPool;

// ============================================================================
// 枚举定义
// ============================================================================

/**
 * @brief 数据库类型枚举
 * 定义系统中所有的数据库类型
 */
enum class DatabaseType {
  DEVICE_DB,      ///< 设备管理数据库
  CONFIG_DB,      ///< 用户配置数据库
  DATA_DB,        ///< 数据管理数据库
  EXPERIMENT_DB,  ///< 实验项目数据库
  SYSTEM_DB       ///< 系统管理数据库
};

/**
 * @brief 表类型枚举
 * 定义系统中所有的数据表类型
 */
enum class TableType {
  // 设备管理数据库表
  CAMERA_INFO,             ///< 相机基本信息表
  CAMERA_CONFIG,           ///< 相机配置表
  CAMERA_STATUS,           ///< 相机状态表
  CALIBRATION_PARAMS,      ///< 标定参数表
  DEVICE_MAINTENANCE,      ///< 设备维护表
  OBJECTIVE_FOCAL_PARAMS,  ///< 物镜焦面参数表

  // 用户配置数据库表
  USER_INFO,             ///< 用户信息表
  USER_CONFIG,           ///< 用户配置表
  EXPOSURE_SETTINGS,     ///< 曝光设置表
  IMAGE_PROCESS_PARAMS,  ///< 图像处理参数表
  ROLE_PERMISSIONS,      ///< 角色权限表
  USER_ROLE_RELATION,    ///< 用户角色关联表

  // 数据管理数据库表
  SYSTEM_LOG,       ///< 系统日志表
  FILE_ATTACHMENT,  ///< 文件附件表

  // 实验项目数据库表
  IMAGE_DATA,          ///< 图像数据表
  PROJECT_MANAGEMENT,  ///< 项目管理表
  EXPERIMENT_PLAN,     ///< 实验计划表
  EXPERIMENT_RECORD,   ///< 实验记录表
  SAMPLE_MANAGEMENT,   ///< 样本管理表
  EXPERIMENT_DATA,     ///< 实验数据表
  EXPERIMENT_REPORT,   ///< 实验报告表
  PROJECT_MEMBER,      ///< 项目成员表

  // 系统管理数据库表
  SYSTEM_PARAMS,         ///< 系统参数表
  DATA_DICTIONARY,       ///< 数据字典表
  BACKUP_RECORD,         ///< 备份记录表
  MESSAGE_NOTIFICATION,  ///< 消息通知表
  OPERATION_AUDIT        ///< 操作审计表
};

/**
 * @brief 日志级别枚举
 */
enum class LogLevel {
  DEBUG,    ///< 调试信息
  INFO,     ///< 一般信息
  WARNING,  ///< 警告信息
  ERROR,    ///< 错误信息
  CRITICAL  ///< 严重错误
};

// ============================================================================
// 工具类和结果类
// ============================================================================

/**
 * @brief 数据库操作结果模板类
 * 用于封装数据库操作的结果，包含成功状态、错误信息和返回数据
 * @tparam T 返回数据的类型
 */
template <typename T>
class DbResult {
 public:
  bool success;          ///< 操作是否成功
  QString errorMessage;  ///< 错误信息
  T data;                ///< 返回的数据

  /**
   * @brief 构造函数
   * @param s 操作是否成功
   * @param msg 错误信息
   * @param d 返回的数据
   */
  DbResult(bool s = false, const QString& msg = "", const T& d = T())
      : success(s), errorMessage(msg), data(d) {}

  /**
   * @brief 创建成功的结果
   * @param data 返回的数据
   * @return 成功的DbResult对象
   */
  static DbResult Success(const T& data = T()) {
    return DbResult(true, "", data);
  }

  /**
   * @brief 创建失败的结果
   * @param msg 错误信息
   * @return 失败的DbResult对象
   */
  static DbResult Error(const QString& msg) {
    return DbResult(false, msg, T());
  }
};

/**
 * @brief 数据库配置结构体
 * 包含数据库连接所需的所有配置信息
 */
struct DatabaseConfig {
  QString dbName;                 ///< 数据库名称
  QString filePath;               ///< 数据库文件路径
  QString connectionName;         ///< 连接名称（用于多连接管理）
  int maxConnections = 10;        ///< 最大连接数
  int busyTimeout = 5000;         ///< 忙等超时时间（毫秒）
  bool enableWAL = true;          ///< 是否启用WAL模式
  bool enableForeignKeys = true;  ///< 是否启用外键约束
  QStringList initSqlList;        ///< 初始化SQL语句列表

  // 新增配置选项
  bool enableQueryCache = true;       ///< 是否启用查询缓存
  int queryCacheSize = 100;           ///< 查询缓存大小
  bool enablePerformanceLog = false;  ///< 是否启用性能日志
  int slowQueryThreshold = 1000;      ///< 慢查询阈值(ms)
  QString configSource;               ///< 配置来源标识

  /**
   * @brief 默认构造函数
   */
  DatabaseConfig() = default;

  /**
   * @brief 构造函数
   * @param name 数据库名称
   * @param path 数据库文件路径
   */
  DatabaseConfig(const QString& name, const QString& path)
      : dbName(name),
        filePath(path),
        connectionName(name + "_" + QUuid::createUuid().toString()) {}

  // 新增静态方法
  /**
   * @brief 从配置文件加载
   * @param configPath 配置文件路径
   * @return 数据库配置
   */
  static DatabaseConfig fromFile(const QString& configPath);

  /**
   * @brief 从环境变量加载
   * @param prefix 环境变量前缀，如"MYAPP_DB_"
   * @return 数据库配置
   */
  static DatabaseConfig fromEnvironment(const QString& prefix = "DB_");

  /**
   * @brief 保存到配置文件
   * @param configPath 配置文件路径
   * @return 是否成功
   */
  bool saveToFile(const QString& configPath) const;

  /**
   * @brief 验证配置有效性
   * @return 验证结果
   */
  DbResult<bool> validate() const;
};

/**
 * @brief 分页参数结构体
 */
struct PageParams {
  int pageIndex = 1;      ///< 页码（从1开始）
  int pageSize = 20;      ///< 每页记录数
  QString orderBy;        ///< 排序字段
  bool ascending = true;  ///< 是否升序

  /**
   * @brief 计算偏移量
   * @return SQL OFFSET值
   */
  int offset() const { return (pageIndex - 1) * pageSize; }

  /**
   * @brief 获取排序SQL片段
   * @return 排序SQL字符串
   */
  QString orderByClause() const {
    if (orderBy.isEmpty()) return "";
    return QString("ORDER BY %1 %2").arg(orderBy, ascending ? "ASC" : "DESC");
  }
};

/**
 * @brief 分页结果结构体
 */
template <typename T>
struct PageResult {
  QList<T> data;        ///< 当前页数据
  int totalCount = 0;   ///< 总记录数
  int totalPages = 0;   ///< 总页数
  int currentPage = 1;  ///< 当前页码
  int pageSize = 20;    ///< 每页记录数

  /**
   * @brief 构造函数
   */
  PageResult() = default;

  /**
   * @brief 构造函数
   * @param list 数据列表
   * @param total 总记录数
   * @param params 分页参数
   */
  PageResult(const QList<T>& list, int total, const PageParams& params)
      : data(list),
        totalCount(total),
        currentPage(params.pageIndex),
        pageSize(params.pageSize) {
    totalPages = (totalCount + pageSize - 1) / pageSize;
  }
};

// ============================================================================
// 基础表操作接口
// ============================================================================

/**
 * @brief 表操作基础接口
 * 定义所有数据表必须实现的基本操作
 */
class ITableOperations {
 public:
  virtual ~ITableOperations() = default;

  /**
   * @brief 创建表
   * @return 是否成功
   */
  virtual bool createTable() = 0;

  /**
   * @brief 删除表
   * @return 是否成功
   */
  virtual bool dropTable() = 0;

  /**
   * @brief 清空表数据
   * @return 是否成功
   */
  virtual bool truncateTable() = 0;

  /**
   * @brief 检查表是否存在
   * @return 是否存在
   */
  virtual bool tableExists() = 0;

  /**
   * @brief 获取表名
   * @return 表名
   */
  virtual QString tableName() const = 0;

  /**
   * @brief 获取表类型
   * @return 表类型
   */
  virtual TableType tableType() const = 0;

  /**
   * @brief 获取记录总数
   * @return 记录总数
   */
  virtual int getTotalCount() const = 0;
};

// ============================================================================
// 基础表操作类（非模板）
// ============================================================================

/**
 * @brief 表操作基础类
 * 提供通用的数据库表操作功能，避免模板类与Q_OBJECT的冲突
 */
class BaseTableOperations : public QObject, public ITableOperations {
  Q_OBJECT

 public:
  QSqlDatabase* m_database;  // 主连接句柄（不拥有）
  mutable QMutex m_mutex;
  QString m_tableName;
  TableType m_tableType;

 protected:
  ConnectionPool* m_pool = nullptr;  // 新增：不拥有的连接池指针

 public:
  // 新增：RAII 连接守卫
  struct ScopedDb {
    QString name;          // 从池里取到的连接名；主连接时为空
    QSqlDatabase db;       // 该次操作用到的 QSqlDatabase 句柄
    ConnectionPool* pool;  // 用于归还连接
    ~ScopedDb();  // 在 .cpp 里实现：若 name 非空，归还连接
  };

  // 新增：获取一个可用的 db（有池则取池连接，否则用主连接）
  ScopedDb acquireDb() const;

  // 构造/析构
  BaseTableOperations(QSqlDatabase* db, const QString& tableName,
                      TableType tableType, ConnectionPool* pool = nullptr,
                      QObject* parent = nullptr);
  ~BaseTableOperations() override = default;

  // ITableOperations
  QString tableName() const override { return m_tableName; }
  TableType tableType() const override { return m_tableType; }
  bool tableExists() override;
  int getTotalCount() const override;
  bool dropTable() override;
  bool truncateTable() override;
  virtual bool createTable() override = 0;

 signals:
  void recordInserted(int id);
  void recordUpdated(int id);
  void recordDeleted(int id);
  void databaseError(const QString& error);

 public:
  bool executeQuery(const QString& sql, const QVariantList& params = {}) const;
  void logOperation(const QString& operation,
                    const QString& details = "") const;
};

// ============================================================================
// 模板辅助类（不使用Q_OBJECT）
// ============================================================================

/**
 * @brief 表操作模板辅助类
 * 提供类型安全的CRUD操作接口，不继承QObject以避免MOC问题
 * @tparam T 数据实体类型
 */
template <typename T>
class BaseTable {
 protected:
  BaseTableOperations* m_baseOps;  ///< 基础操作对象指针

 public:
  /**
   * @brief 构造函数
   * @param baseOps 基础操作对象指针
   */
  explicit BaseTable(BaseTableOperations* baseOps) : m_baseOps(baseOps) {}

  /**
   * @brief 析构函数
   */
  virtual ~BaseTable() = default;

  // 获取基础操作对象
  BaseTableOperations* baseOperations() const { return m_baseOps; }

  // ========================================================================
  // 纯虚函数，子类必须实现
  // ========================================================================

  /**
   * @brief 添加记录
   * @param entity 数据实体
   * @return 操作结果，包含新记录的ID
   */
  virtual DbResult<int> insert(const T& entity) = 0;

  /**
   * @brief 更新记录
   * @param entity 数据实体
   * @return 操作结果
   */
  virtual DbResult<bool> update(const T& entity) = 0;

  /**
   * @brief 根据ID删除记录
   * @param id 记录ID
   * @return 操作结果
   */
  virtual DbResult<bool> deleteById(int id) = 0;

  /**
   * @brief 根据ID查询记录
   * @param id 记录ID
   * @return 操作结果，包含查询到的记录
   */
  virtual DbResult<T> selectById(int id) const = 0;

  /**
   * @brief 查询所有记录
   * @return 操作结果，包含所有记录列表
   */
  virtual DbResult<QList<T>> selectAll() const = 0;

  /**
   * @brief 分页查询记录
   * @param params 分页参数
   * @return 操作结果，包含分页结果
   */
  virtual DbResult<PageResult<T>> selectByPage(
      const PageParams& params) const = 0;

  /**
   * @brief 批量插入记录
   * @param entities 数据实体列表
   * @return 操作结果，包含成功插入的记录数
   */
  virtual DbResult<int> batchInsert(const QList<T>& entities) = 0;
};

#endif  // DATABASE_FRAMEWORK_H
