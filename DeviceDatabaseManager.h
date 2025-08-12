// DeviceDatabaseManager.h - 设备管理数据库管理器
#ifndef DEVICE_DATABASE_MANAGER_H
#define DEVICE_DATABASE_MANAGER_H

#include <QDateTime>
#include <QRegularExpression>
#include <QSet>

#include "BaseDatabaseManager.h"
#include "DatabaseFramework.h"

// ============================================================================
// 数据实体定义
// ============================================================================

/**
 * @brief 相机基本信息实体
 * 对应数据库中的相机基本信息表
 */
struct CameraInfo {
  int id = -1;             ///< 相机ID（主键，自增）
  QString name;            ///< 相机名称
  QString version;         ///< 相机版本
  QString connectionType;  ///< 连接方式（USB、Ethernet等）
  QString serialNumber;    ///< 设备序列号（唯一）
  QString manufacturer;    ///< 制造商
  QDateTime createdAt;     ///< 创建时间
  QDateTime updatedAt;     ///< 更新时间

  /**
   * @brief 默认构造函数
   */
  CameraInfo() {
    QDateTime now = QDateTime::currentDateTime();
    createdAt = now;
    updatedAt = now;
  }

  /**
   * @brief 验证数据有效性
   * @return 数据是否有效
   */
  bool isValid() const { return !name.isEmpty() && !serialNumber.isEmpty(); }

  /**
   * @brief 比较操作符
   * @param other 另一个CameraInfo对象
   * @return 是否相等
   */
  bool operator==(const CameraInfo& other) const {
    return id == other.id && serialNumber == other.serialNumber;
  }
};

/**
 * @brief 相机配置实体
 * 对应数据库中的相机配置表
 */
struct CameraConfig {
  int id = -1;                    ///< 配置ID（主键，自增）
  int cameraId = -1;              ///< 相机ID（外键）
  QString resolution;             ///< 分辨率（如"1920x1080"）
  double frameRate = 0.0;         ///< 帧率
  QString exposureRange;          ///< 曝光值范围（如"0.1-1000ms"）
  QString gainRange;              ///< 增益值范围（如"1-100"）
  QString acquisitionStrategy;    ///< 采集策略
  QString supportedImagingModes;  ///< 支持的成像模式（JSON格式）
  QDateTime createdAt;            ///< 创建时间
  QDateTime updatedAt;            ///< 更新时间

  /**
   * @brief 默认构造函数
   */
  CameraConfig() {
    QDateTime now = QDateTime::currentDateTime();
    createdAt = now;
    updatedAt = now;
  }

  /**
   * @brief 验证数据有效性
   * @return 数据是否有效
   */
  bool isValid() const { return cameraId > 0 && !resolution.isEmpty(); }
};

/**
 * @brief 相机状态实体
 * 对应数据库中的相机状态表
 */
struct CameraStatus {
  int id = -1;                    ///< 状态ID（主键，自增）
  int cameraId = -1;              ///< 相机ID（外键）
  double currentFrameRate = 0.0;  ///< 当前帧率
  double currentGain = 0.0;       ///< 当前增益值
  double currentExposure = 0.0;   ///< 当前曝光值
  bool autoExposure = false;      ///< 是否自动曝光
  bool autoGain = false;          ///< 是否自动增益
  bool onlineStatus = false;      ///< 在线状态
  QDateTime lastHeartbeat;        ///< 最后心跳时间
  QDateTime updatedAt;            ///< 更新时间

  /**
   * @brief 默认构造函数
   */
  CameraStatus() {
    QDateTime now = QDateTime::currentDateTime();
    lastHeartbeat = now;
    updatedAt = now;
  }

  /**
   * @brief 验证数据有效性
   * @return 数据是否有效
   */
  bool isValid() const { return cameraId > 0; }

  /**
   * @brief 检查设备是否在线
   * @param timeoutSeconds 超时时间（秒）
   * @return 是否在线
   */
  bool isOnline(int timeoutSeconds = 30) const {
    if (!onlineStatus) return false;
    return lastHeartbeat.secsTo(QDateTime::currentDateTime()) <= timeoutSeconds;
  }
};

// ============================================================================
// 相机信息表操作类
// ============================================================================

/**
 * @brief 相机基本信息表操作类
 * 继承自BaseTableOperations并实现createTable方法
 */
class CameraInfoTableOperations : public BaseTableOperations {
  Q_OBJECT
 public:
  explicit CameraInfoTableOperations(QSqlDatabase* db, ConnectionPool* pool);
  ~CameraInfoTableOperations() override = default;

  bool createTable() override;

 private:
  static const QString CREATE_TABLE_SQL;
};

/**
 * @brief 相机基本信息表业务逻辑类
 * 提供相机基本信息的CRUD操作
 */
class CameraInfoTable : public BaseTable<CameraInfo> {
 private:
  // SQL语句常量
  static const QString INSERT_SQL;
  static const QString UPDATE_SQL;
  static const QString DELETE_SQL;
  static const QString SELECT_BY_ID_SQL;
  static const QString SELECT_ALL_SQL;
  static const QString SELECT_BY_SERIAL_SQL;
  static const QString SEARCH_SQL;
  static const QString COUNT_SQL;
  static const QString CHECK_SERIAL_EXISTS_SQL;

  CameraInfoTableOperations* m_ops;  ///< 基础操作对象指针

 public:
  /**
   * @brief 构造函数
   * @param db 数据库连接指针
   * @param parent 父对象
   */
  explicit CameraInfoTable(QSqlDatabase* db, ConnectionPool* pool,
                           QObject* parent = nullptr);
  /**
   * @brief 析构函数
   */
  ~CameraInfoTable() override;

  // ========================================================================
  // 实现BaseTable虚函数
  // ========================================================================

  /**
   * @brief 插入相机信息
   * @param camera 相机信息
   * @return 操作结果，包含新记录的ID
   */
  DbResult<int> insert(const CameraInfo& camera) override;

  /**
   * @brief 更新相机信息
   * @param camera 相机信息
   * @return 操作结果
   */
  DbResult<bool> update(const CameraInfo& camera) override;

  /**
   * @brief 根据ID删除相机
   * @param id 相机ID
   * @return 操作结果
   */
  DbResult<bool> deleteById(int id) override;

  /**
   * @brief 根据ID查询相机
   * @param id 相机ID
   * @return 操作结果，包含相机信息
   */
  DbResult<CameraInfo> selectById(int id) const override;

  /**
   * @brief 查询所有相机
   * @return 操作结果，包含所有相机列表
   */
  DbResult<QList<CameraInfo>> selectAll() const override;

  /**
   * @brief 分页查询相机
   * @param params 分页参数
   * @return 操作结果，包含分页结果
   */
  DbResult<PageResult<CameraInfo>> selectByPage(
      const PageParams& params) const override;

  /**
   * @brief 批量插入相机
   * @param cameras 相机信息列表
   * @return 操作结果，包含成功插入的记录数
   */
  DbResult<int> batchInsert(const QList<CameraInfo>& cameras) override;

  // ========================================================================
  // 扩展功能方法
  // ========================================================================

  /**
   * @brief 根据序列号查询相机
   * @param serialNumber 序列号
   * @return 操作结果，包含相机信息
   */
  DbResult<CameraInfo> selectBySerialNumber(const QString& serialNumber) const;

  /**
   * @brief 检查序列号是否存在
   * @param serialNumber 序列号
   * @param excludeId 排除的ID（用于更新时检查）
   * @return 是否存在
   */
  bool serialNumberExists(const QString& serialNumber,
                          int excludeId = -1) const;

  /**
   * @brief 搜索相机
   * @param keyword 关键词（在名称和制造商中搜索）
   * @return 操作结果，包含搜索结果
   */
  DbResult<QList<CameraInfo>> search(const QString& keyword) const;

  /**
   * @brief 根据制造商查询相机
   * @param manufacturer 制造商
   * @return 操作结果，包含相机列表
   */
  DbResult<QList<CameraInfo>> selectByManufacturer(
      const QString& manufacturer) const;

  /**
   * @brief 获取所有制造商列表
   * @return 制造商列表
   */
  QStringList getAllManufacturers() const;

  /**
   * @brief 根据连接类型查询相机
   * @param connectionType 连接类型
   * @return 操作结果，包含相机列表
   */
  DbResult<QList<CameraInfo>> selectByConnectionType(
      const QString& connectionType) const;

  /**
   * @brief 获取基础操作对象
   * @return 基础操作对象指针
   */
  CameraInfoTableOperations* operations() const { return m_ops; }

 private:
  /**
   * @brief 从查询结果构建CameraInfo对象
   * @param query SQL查询对象
   * @return CameraInfo对象
   */
  CameraInfo buildCameraInfo(const QSqlQuery& query) const;

  /**
   * @brief 验证相机信息
   * @param camera 相机信息
   * @param isUpdate 是否为更新操作
   * @return 验证结果
   */
  DbResult<bool> validateCameraInfo(const CameraInfo& camera,
                                    bool isUpdate = false) const;

  static inline QString sanitizeOrderBy(const QString& col) {
    static const QSet<QString> k = {"id",
                                    "name",
                                    "version",
                                    "connection_type",
                                    "serial_number",
                                    "manufacturer",
                                    "created_at",
                                    "updated_at"};
    return k.contains(col) ? col : "name";
  }
};

// ============================================================================
// 设备数据库管理器
// ============================================================================

/**
 * @brief 设备管理数据库管理器
 * 管理设备相关的所有数据表
 */
class DeviceDatabaseManager : public BaseDatabaseManager {
  Q_OBJECT

 private:
  std::unique_ptr<CameraInfoTable> m_cameraInfoTable;  ///< 相机信息表

 public:
  /**
   * @brief 构造函数
   * @param config 数据库配置
   * @param parent 父对象
   */
  explicit DeviceDatabaseManager(const DatabaseConfig& config,
                                 QObject* parent = nullptr);

  /**
   * @brief 析构函数
   */
  ~DeviceDatabaseManager() override = default;

  // ========================================================================
  // 表访问器
  // ========================================================================

  /**
   * @brief 获取相机信息表操作对象
   * @return 相机信息表指针
   */
  CameraInfoTable* cameraInfoTable() const;

  // 后续可以添加其他表的访问器
  // CameraConfigTable* cameraConfigTable() const;
  // CameraStatusTable* cameraStatusTable() const;
  // CalibrationParamsTable* calibrationParamsTable() const;
  // DeviceMaintenanceTable* deviceMaintenanceTable() const;
  // ObjectiveFocalParamsTable* objectiveFocalParamsTable() const;

  // ========================================================================
  // 业务逻辑方法
  // ========================================================================

  /**
   * @brief 添加新相机
   * @param camera 相机信息
   * @return 操作结果，包含新相机ID
   */
  DbResult<int> addCamera(const CameraInfo& camera);

  /**
   * @brief 更新相机信息
   * @param camera 相机信息
   * @return 操作结果
   */
  DbResult<bool> updateCamera(const CameraInfo& camera);

  /**
   * @brief 删除相机
   * @param cameraId 相机ID
   * @return 操作结果
   */
  DbResult<bool> removeCamera(int cameraId);

  /**
   * @brief 获取相机信息
   * @param cameraId 相机ID
   * @return 操作结果，包含相机信息
   */
  DbResult<CameraInfo> getCamera(int cameraId) const;

  /**
   * @brief 获取所有相机
   * @return 操作结果，包含所有相机列表
   */
  DbResult<QList<CameraInfo>> getAllCameras() const;

  /**
   * @brief 根据序列号获取相机
   * @param serialNumber 序列号
   * @return 操作结果，包含相机信息
   */
  DbResult<CameraInfo> getCameraBySerialNumber(
      const QString& serialNumber) const;

  /**
   * @brief 搜索相机
   * @param keyword 关键词
   * @return 操作结果，包含搜索结果
   */
  DbResult<QList<CameraInfo>> searchCameras(const QString& keyword) const;

  /**
   * @brief 批量导入相机
   * @param cameras 相机列表
   * @return 操作结果，包含成功导入的数量
   */
  DbResult<int> importCameras(const QList<CameraInfo>& cameras);

  /**
   * @brief 获取相机统计信息
   * @return 统计信息映射（制造商 -> 数量）
   */
  QMap<QString, int> getCameraStatistics() const;

 protected:
  /**
   * @brief 注册所有表
   * 实现基类纯虚函数
   */
  void registerTables() override;

 signals:
  /**
   * @brief 相机添加信号
   * @param cameraId 相机ID
   */
  void cameraAdded(int cameraId);

  /**
   * @brief 相机更新信号
   * @param cameraId 相机ID
   */
  void cameraUpdated(int cameraId);

  /**
   * @brief 相机删除信号
   * @param cameraId 相机ID
   */
  void cameraRemoved(int cameraId);
};

#endif  // DEVICE_DATABASE_MANAGER_H
