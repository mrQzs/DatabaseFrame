// DeviceDatabaseManager.h - 设备管理数据库管理器
#ifndef DEVICE_DATABASE_MANAGER_H
#define DEVICE_DATABASE_MANAGER_H

#include <QDateTime>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include "BaseDatabaseManager.h"
#include "DatabaseFramework.h"
#include "DeviceDataBaseStruct.h"

class CameraInfoTable;

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

  void close() override;

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
