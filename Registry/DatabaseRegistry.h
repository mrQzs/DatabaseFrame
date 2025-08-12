// DatabaseRegistry.h - 数据库注册中心
#ifndef DATABASE_REGISTRY_H
#define DATABASE_REGISTRY_H

#include <QDir>
#include <QMutex>
#include <QObject>
#include <QStandardPaths>
#include <memory>
#include <unordered_map>

#include "BaseDatabaseManager.h"
#include "DeviceDatabaseManager.h"

/**
 * @brief 数据库注册中心
 * 统一管理所有数据库实例，提供单一访问入口
 * 采用单例模式，确保全局唯一性
 */
class DatabaseRegistry : public QObject {
  Q_OBJECT

 private:
  static std::unique_ptr<DatabaseRegistry> s_instance;  ///< 单例实例
  static QMutex s_instanceMutex;  ///< 实例创建互斥锁

  mutable QMutex m_registryMutex;  ///< 注册表互斥锁
  QString m_baseDataPath;          ///< 数据库文件基础路径
  bool m_initialized = false;      ///< 是否已初始化

  // 数据库管理器映射
  std::unordered_map<DatabaseType, std::unique_ptr<BaseDatabaseManager>>
      m_databases;

  /**
   * @brief 私有构造函数（单例模式）
   * @param parent 父对象
   */
  explicit DatabaseRegistry(QObject* parent = nullptr);

 public:
  /**
   * @brief 析构函数
   */
  ~DatabaseRegistry() override;

  // 禁用拷贝构造和赋值操作符
  DatabaseRegistry(const DatabaseRegistry&) = delete;
  DatabaseRegistry& operator=(const DatabaseRegistry&) = delete;

  /**
   * @brief 获取单例实例
   * @return 数据库注册中心实例指针
   */
  static DatabaseRegistry* getInstance();

  /**
   * @brief 销毁单例实例
   * 主要用于程序退出时的清理
   */
  static void destroyInstance();

  // ========================================================================
  // 初始化和配置
  // ========================================================================

  /**
   * @brief 初始化数据库注册中心
   * @param dataPath 数据库文件基础路径（可选，默认使用系统路径）
   * @return 是否成功
   */
  bool initialize(const QString& dataPath = QString());

  /**
   * @brief 关闭所有数据库连接
   */
  void shutdown();

  /**
   * @brief 检查是否已初始化
   * @return 是否已初始化
   */
  bool isInitialized() const { return m_initialized; }

  /**
   * @brief 获取数据库基础路径
   * @return 基础路径
   */
  QString basePath() const { return m_baseDataPath; }

  // ========================================================================
  // 数据库访问接口
  // ========================================================================

  /**
   * @brief 获取设备管理数据库
   * @return 设备数据库管理器指针
   */
  DeviceDatabaseManager* deviceDatabase() const;

  /**
   * @brief 获取指定类型的数据库管理器
   * @param dbType 数据库类型
   * @return 数据库管理器指针（如果不存在返回nullptr）
   */
  BaseDatabaseManager* getDatabase(DatabaseType dbType) const;

  /**
   * @brief 检查指定数据库是否存在且已初始化
   * @param dbType 数据库类型
   * @return 是否存在且已初始化
   */
  bool isDatabaseAvailable(DatabaseType dbType) const;

  // ========================================================================
  // 数据库管理操作
  // ========================================================================

  /**
   * @brief 创建所有数据库
   * @return 成功创建的数据库数量
   */
  int createAllDatabases();

  /**
   * @brief 备份所有数据库
   * @param backupDir 备份目录路径
   * @return 操作结果，包含备份成功的数据库数量
   */
  DbResult<int> backupAllDatabases(const QString& backupDir);

  /**
   * @brief 恢复所有数据库
   * @param backupDir 备份目录路径
   * @return 操作结果，包含恢复成功的数据库数量
   */
  DbResult<int> restoreAllDatabases(const QString& backupDir);

  /**
   * @brief 获取所有数据库的健康状态
   * @return 健康状态映射（数据库类型 -> 是否健康）
   */
  QMap<DatabaseType, bool> getDatabaseHealthStatus() const;

  /**
   * @brief 获取所有数据库的统计信息
   * @return 统计信息映射
   */
  QMap<DatabaseType, BaseDatabaseManager::DatabaseStats> getAllDatabaseStats()
      const;

  /**
   * @brief 优化所有数据库
   * @return 操作结果，包含优化成功的数据库数量
   */
  DbResult<int> optimizeAllDatabases();

  // ========================================================================
  // 配置管理
  // ========================================================================

  /**
   * @brief 获取默认数据库配置
   * @param dbType 数据库类型
   * @return 数据库配置
   */
  DatabaseConfig getDefaultConfig(DatabaseType dbType) const;

  /**
   * @brief 设置自定义数据库配置
   * @param dbType 数据库类型
   * @param config 自定义配置
   */
  void setCustomConfig(DatabaseType dbType, const DatabaseConfig& config);

  /**
   * @brief 重置为默认配置
   * @param dbType 数据库类型
   */
  void resetToDefaultConfig(DatabaseType dbType);

 signals:
  /**
   * @brief 数据库注册中心初始化完成信号
   * @param success 是否成功
   * @param message 结果消息
   */
  void initializationCompleted(bool success, const QString& message);

  /**
   * @brief 数据库连接状态变化信号
   * @param dbType 数据库类型
   * @param connected 是否已连接
   */
  void databaseConnectionChanged(DatabaseType dbType, bool connected);

  /**
   * @brief 数据库错误信号
   * @param dbType 数据库类型
   * @param error 错误信息
   */
  void databaseError(DatabaseType dbType, const QString& error);

  /**
   * @brief 数据库健康检查完成信号
   * @param healthStatus 健康状态映射
   */
  void healthCheckCompleted(const QMap<DatabaseType, bool>& healthStatus);

 private:
  /**
   * @brief 注册单个数据库
   * @param dbType 数据库类型
   * @return 是否成功
   */
  bool registerDatabase(DatabaseType dbType);

  /**
   * @brief 创建数据库配置
   * @param dbType 数据库类型
   * @return 数据库配置
   */
  DatabaseConfig createDatabaseConfig(DatabaseType dbType) const;

  /**
   * @brief 获取数据库类型的字符串名称
   * @param dbType 数据库类型
   * @return 字符串名称
   */
  QString getDatabaseTypeName(DatabaseType dbType) const;

  /**
   * @brief 确保数据库目录存在
   * @return 是否成功
   */
  bool ensureDataDirectoryExists();

  /**
   * @brief 连接数据库信号槽
   * @param database 数据库管理器指针
   * @param dbType 数据库类型
   */
  void connectDatabaseSignals(BaseDatabaseManager* database,
                              DatabaseType dbType);

 private slots:
  /**
   * @brief 处理数据库初始化完成
   * @param success 是否成功
   */
  void onDatabaseInitialized(bool success);

  /**
   * @brief 处理数据库错误
   * @param error 错误信息
   */
  void onDatabaseError(const QString& error);

  /**
   * @brief 处理数据库健康检查完成
   * @param healthy 是否健康
   */
  void onHealthCheckCompleted(bool healthy);
};

// ============================================================================
// 便利宏定义
// ============================================================================

/**
 * @brief 便利宏：获取设备数据库
 */
#define DEVICE_DB() DatabaseRegistry::getInstance()->deviceDatabase()

/**
 * @brief 便利宏：获取指定类型数据库
 */
#define GET_DB(type) \
  DatabaseRegistry::getInstance()->getDatabase(DatabaseType::type)

/**
 * @brief 便利宏：检查数据库是否可用
 */
#define IS_DB_AVAILABLE(type) \
  DatabaseRegistry::getInstance()->isDatabaseAvailable(DatabaseType::type)

#endif  // DATABASE_REGISTRY_H
