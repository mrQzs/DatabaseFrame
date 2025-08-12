// ============================================================================
// DeviceDatabaseManager.cpp - 实现文件
// ============================================================================

#include "DeviceDatabaseManager.h"

#include "CameraInfoTable.h"

// ============================================================================
// DeviceDatabaseManager实现
// ============================================================================

DeviceDatabaseManager::DeviceDatabaseManager(const DatabaseConfig& config,
                                             QObject* parent)
    : BaseDatabaseManager(DatabaseType::DEVICE_DB, config, parent) {
  qInfo() << "创建设备数据库管理器";
}

void DeviceDatabaseManager::close() {
  m_cameraInfoTable.reset();     // 先释放业务表，避免悬空
  BaseDatabaseManager::close();  // 再做通用清理
}

void DeviceDatabaseManager::registerTables() {
  // 用连接池实例化表（关键改动）
  m_cameraInfoTable =
      std::make_unique<CameraInfoTable>(&m_database, m_connectionPool.get());

  // 信号连接
  connect(m_cameraInfoTable->operations(), &BaseTableOperations::recordInserted,
          this, &DeviceDatabaseManager::cameraAdded);
  connect(m_cameraInfoTable->operations(), &BaseTableOperations::recordUpdated,
          this, &DeviceDatabaseManager::cameraUpdated);
  connect(m_cameraInfoTable->operations(), &BaseTableOperations::recordDeleted,
          this, &DeviceDatabaseManager::cameraRemoved);
  connect(m_cameraInfoTable->operations(), &BaseTableOperations::databaseError,
          this, &DeviceDatabaseManager::databaseError);

  // 交给基类接管“所有权”（唯一所有者）
  registerTable(TableType::CAMERA_INFO, std::unique_ptr<ITableOperations>(
                                            m_cameraInfoTable->operations()));

  // 后续可以注册其他表
  // registerTable(TableType::CAMERA_CONFIG,
  // std::make_unique<CameraConfigTable>(&m_database, this));
  // registerTable(TableType::CAMERA_STATUS,
  // std::make_unique<CameraStatusTable>(&m_database, this));
  // ...
}

CameraInfoTable* DeviceDatabaseManager::cameraInfoTable() const {
  return m_cameraInfoTable.get();
}

DbResult<int> DeviceDatabaseManager::addCamera(const CameraInfo& camera) {
  if (!m_cameraInfoTable) {
    return DbResult<int>::Error("相机信息表未初始化");
  }

  return m_cameraInfoTable->insert(camera);
}

DbResult<bool> DeviceDatabaseManager::updateCamera(const CameraInfo& camera) {
  if (!m_cameraInfoTable) {
    return DbResult<bool>::Error("相机信息表未初始化");
  }

  return m_cameraInfoTable->update(camera);
}

DbResult<bool> DeviceDatabaseManager::removeCamera(int cameraId) {
  if (!m_cameraInfoTable) {
    return DbResult<bool>::Error("相机信息表未初始化");
  }

  return m_cameraInfoTable->deleteById(cameraId);
}

DbResult<CameraInfo> DeviceDatabaseManager::getCamera(int cameraId) const {
  if (!m_cameraInfoTable) {
    return DbResult<CameraInfo>::Error("相机信息表未初始化");
  }

  return m_cameraInfoTable->selectById(cameraId);
}

DbResult<QList<CameraInfo>> DeviceDatabaseManager::getAllCameras() const {
  if (!m_cameraInfoTable) {
    return DbResult<QList<CameraInfo>>::Error("相机信息表未初始化");
  }

  return m_cameraInfoTable->selectAll();
}

DbResult<CameraInfo> DeviceDatabaseManager::getCameraBySerialNumber(
    const QString& serialNumber) const {
  if (!m_cameraInfoTable) {
    return DbResult<CameraInfo>::Error("相机信息表未初始化");
  }

  return m_cameraInfoTable->selectBySerialNumber(serialNumber);
}

DbResult<QList<CameraInfo>> DeviceDatabaseManager::searchCameras(
    const QString& keyword) const {
  if (!m_cameraInfoTable) {
    return DbResult<QList<CameraInfo>>::Error("相机信息表未初始化");
  }

  return m_cameraInfoTable->search(keyword);
}

DbResult<int> DeviceDatabaseManager::importCameras(
    const QList<CameraInfo>& cameras) {
  if (!m_cameraInfoTable) {
    return DbResult<int>::Error("相机信息表未初始化");
  }

  return m_cameraInfoTable->batchInsert(cameras);
}

QMap<QString, int> DeviceDatabaseManager::getCameraStatistics() const {
  QMap<QString, int> statistics;

  if (!m_cameraInfoTable) {
    return statistics;
  }

  auto allCameras = m_cameraInfoTable->selectAll();
  if (!allCameras.success) {
    return statistics;
  }

  for (const auto& camera : allCameras.data) {
    QString manufacturer =
        camera.manufacturer.isEmpty() ? "未知" : camera.manufacturer;
    statistics[manufacturer]++;
  }

  return statistics;
}
