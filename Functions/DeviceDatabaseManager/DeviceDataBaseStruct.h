#ifndef DEVICEDATABASESTRUCT_H
#define DEVICEDATABASESTRUCT_H

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

#endif  // DEVICEDATABASESTRUCT_H
