#include "CameraInfoTable.h"

#include <QSet>
#include <QStringList>

// ============================================================================
// CameraInfoTable SQL语句常量定义
// ============================================================================

const QString CameraInfoTable::INSERT_SQL = R"(
    INSERT INTO camera_info (name, version, connection_type, serial_number, manufacturer, created_at, updated_at)
    VALUES (?, ?, ?, ?, ?, ?, ?)
)";

const QString CameraInfoTable::UPDATE_SQL = R"(
    UPDATE camera_info
    SET name = ?, version = ?, connection_type = ?, serial_number = ?,
        manufacturer = ?, updated_at = ?
    WHERE id = ?
)";

const QString CameraInfoTable::DELETE_SQL = R"(
    DELETE FROM camera_info WHERE id = ?
)";

const QString CameraInfoTable::SELECT_BY_ID_SQL = R"(
    SELECT id, name, version, connection_type, serial_number, manufacturer, created_at, updated_at
    FROM camera_info WHERE id = ?
)";

const QString CameraInfoTable::SELECT_ALL_SQL = R"(
    SELECT id, name, version, connection_type, serial_number, manufacturer, created_at, updated_at
    FROM camera_info ORDER BY name
)";

const QString CameraInfoTable::SELECT_BY_SERIAL_SQL = R"(
    SELECT id, name, version, connection_type, serial_number, manufacturer, created_at, updated_at
    FROM camera_info WHERE serial_number = ?
)";

const QString CameraInfoTable::SEARCH_SQL = R"(
    SELECT id, name, version, connection_type, serial_number, manufacturer, created_at, updated_at
    FROM camera_info
    WHERE name LIKE ? OR manufacturer LIKE ? OR serial_number LIKE ?
    ORDER BY name
)";

const QString CameraInfoTable::COUNT_SQL = R"(
    SELECT COUNT(*) FROM camera_info
)";

const QString CameraInfoTable::CHECK_SERIAL_EXISTS_SQL = R"(
    SELECT COUNT(*) FROM camera_info WHERE serial_number = ? AND id != ?
)";

// ============================================================================
// CameraInfoTableOperations 实现
// ============================================================================

const QString CameraInfoTableOperations::CREATE_TABLE_SQL = R"(
  CREATE TABLE IF NOT EXISTS camera_info (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    version TEXT,
    connection_type TEXT,
    serial_number TEXT UNIQUE NOT NULL,
    manufacturer TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    CHECK(length(name) > 0),
    CHECK(length(serial_number) > 0)
  )
)";

CameraInfoTableOperations::CameraInfoTableOperations(QSqlDatabase* db,
                                                     ConnectionPool* pool)
    : BaseTableOperations(db, "camera_info", TableType::CAMERA_INFO, pool,
                          nullptr) {
  logOperation("构造函数", "相机信息表操作对象已创建");
}

bool CameraInfoTableOperations::createTable() {
  qDebug() << "CameraInfoTableOperations::createTable() 开始";

  QMutexLocker locker(&m_mutex);
  qDebug() << "获取互斥锁成功";

  auto c = acquireDb();
  qDebug() << "获取数据库连接:" << c.name << "isOpen:" << c.db.isOpen();

  if (!c.db.isOpen()) {
    qCritical() << "数据库连接未打开!";
    return false;
  }

  QSqlQuery query(c.db);
  qDebug() << "创建QSqlQuery对象成功";

  qDebug() << "执行CREATE TABLE SQL...";
  bool ok = query.exec(CREATE_TABLE_SQL);
  qDebug() << "CREATE TABLE 执行结果:" << ok;

  if (!ok) {
    QString error = query.lastError().text();
    qCritical() << "创建表SQL执行失败:" << error;
    logOperation("创建表失败", error);
    return false;
  }

  qDebug() << "开始创建触发器...";
  // 触发器创建
  const char* TRG_SQL = R"(
      CREATE TRIGGER IF NOT EXISTS trg_camera_info_updated_at
      AFTER UPDATE ON camera_info
      FOR EACH ROW BEGIN
        UPDATE camera_info SET updated_at = CURRENT_TIMESTAMP WHERE id = NEW.id;
      END;
    )";

  bool triggerOk = query.exec(TRG_SQL);
  qDebug() << "触发器创建结果:" << triggerOk;

  if (!triggerOk) {
    qWarning() << "创建触发器失败:" << query.lastError().text();
    logOperation("创建触发器失败", query.lastError().text());
  }

  qDebug() << "开始创建索引...";
  // 创建索引
  bool idx1 = query.exec(
      "CREATE INDEX IF NOT EXISTS idx_camera_info_mfr ON "
      "camera_info(manufacturer)");
  qDebug() << "索引1创建结果:" << idx1;

  bool idx2 = query.exec(
      "CREATE INDEX IF NOT EXISTS idx_camera_info_conn ON "
      "camera_info(connection_type)");
  qDebug() << "索引2创建结果:" << idx2;

  logOperation("创建表成功", m_tableName);
  qDebug() << "CameraInfoTableOperations::createTable() 完成，返回true";

  return true;
}

// ============================================================================
// CameraInfoTable实现
// ============================================================================

CameraInfoTable::CameraInfoTable(QSqlDatabase* db, ConnectionPool* pool,
                                 QObject*)
    : BaseTable<CameraInfo>(nullptr) {
  m_ops = new CameraInfoTableOperations(db, pool);  // QPointer 自动追踪生命周期
  m_baseOps = m_ops;
  m_ops->logOperation("构造函数", "相机信息表业务逻辑对象已创建");
}

CameraInfoTable::~CameraInfoTable() { m_baseOps = nullptr; }

DbResult<int> CameraInfoTable::insert(const CameraInfo& camera) {
  if (!m_ops) {
    return DbResult<int>::Error("相机信息表未初始化或已释放");
  }
  qInfo() << "=== 开始插入相机 ===";
  qInfo() << "相机名称:" << camera.name;
  qInfo() << "序列号:" << camera.serialNumber;
  qInfo() << "制造商:" << camera.manufacturer;

  // 验证数据
  auto validation = validateCameraInfo(camera, false);
  if (!validation.success) {
    qCritical() << "数据验证失败:" << validation.errorMessage;
    return DbResult<int>::Error(validation.errorMessage);
  }
  qInfo() << "数据验证通过";

  // 检查序列号是否已存在
  if (serialNumberExists(camera.serialNumber)) {
    qCritical() << "序列号冲突:" << camera.serialNumber;
    return DbResult<int>::Error(
        QString("序列号已存在: %1").arg(camera.serialNumber));
  }
  qInfo() << "序列号检查通过";

  // ✅ 统一使用连接池
  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) {
    return DbResult<int>::Error("数据库未打开");
  }
  qInfo() << "数据库连接正常";

  QMutexLocker locker(&m_ops->m_mutex);
  QSqlQuery query(c.db);  // ✅ 使用池连接而不是主连接
  query.prepare(INSERT_SQL);
  qInfo() << "SQL语句:" << INSERT_SQL;

  QDateTime now = QDateTime::currentDateTime();

  query.addBindValue(camera.name);
  query.addBindValue(camera.version);
  query.addBindValue(camera.connectionType);
  query.addBindValue(camera.serialNumber);
  query.addBindValue(camera.manufacturer);
  query.addBindValue(now);
  query.addBindValue(now);

  qInfo() << "绑定参数完成，开始执行SQL";

  if (!query.exec()) {
    QString error =
        QString("插入相机信息失败: %1").arg(query.lastError().text());
    qCritical() << "SQL执行失败:" << error;
    qCritical() << "最后执行的SQL:" << query.lastQuery();
    qCritical() << "绑定的值:" << query.boundValues();
    m_ops->logOperation("插入失败", error);
    emit m_ops->databaseError(error);
    return DbResult<int>::Error(error);
  }

  int newId = query.lastInsertId().toInt();
  qInfo() << "SQL执行成功，新ID:" << newId;

  if (newId <= 0) {
    qCritical() << "获取新ID失败，lastInsertId()返回:" << query.lastInsertId();
    return DbResult<int>::Error("获取新记录ID失败");
  }

  m_ops->logOperation(
      "插入成功",
      QString("新相机ID: %1, 序列号: %2").arg(newId).arg(camera.serialNumber));
  emit m_ops->recordInserted(newId);

  qInfo() << "=== 插入相机完成 ===";
  return DbResult<int>::Success(newId);
}

DbResult<bool> CameraInfoTable::update(const CameraInfo& camera) {
  if (!m_ops) {
    return DbResult<bool>::Error("相机信息表未初始化或已释放");
  }
  if (camera.id <= 0) {
    return DbResult<bool>::Error("无效的相机ID");
  }

  // 验证数据
  auto validation = validateCameraInfo(camera, true);
  if (!validation.success) {
    return DbResult<bool>::Error(validation.errorMessage);
  }

  // 检查序列号是否被其他记录使用
  if (serialNumberExists(camera.serialNumber, camera.id)) {
    return DbResult<bool>::Error(
        QString("序列号已被其他设备使用: %1").arg(camera.serialNumber));
  }

  // ✅ 统一使用连接池
  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) {
    return DbResult<bool>::Error("数据库未打开");
  }
  qInfo() << "数据库连接正常";

  QMutexLocker locker(&m_ops->m_mutex);
  QSqlQuery query(c.db);  // ✅ 使用池连接而不是主连接
  query.prepare(UPDATE_SQL);
  qInfo() << "SQL语句:" << UPDATE_SQL;

  QDateTime now = QDateTime::currentDateTime();

  query.addBindValue(camera.name);
  query.addBindValue(camera.version);
  query.addBindValue(camera.connectionType);
  query.addBindValue(camera.serialNumber);
  query.addBindValue(camera.manufacturer);
  query.addBindValue(now);
  query.addBindValue(camera.id);

  if (!query.exec()) {
    QString error =
        QString("更新相机信息失败: %1").arg(query.lastError().text());
    m_ops->logOperation("更新失败", error);
    emit m_ops->databaseError(error);
    return DbResult<bool>::Error(error);
  }

  if (query.numRowsAffected() == 0) {
    return DbResult<bool>::Error("未找到指定的相机记录");
  }

  m_ops->logOperation("更新成功", QString("相机ID: %1, 序列号: %2")
                                      .arg(camera.id)
                                      .arg(camera.serialNumber));
  emit m_ops->recordUpdated(camera.id);

  return DbResult<bool>::Success(true);
}

DbResult<bool> CameraInfoTable::deleteById(int id) {
  if (!m_ops) {
    return DbResult<bool>::Error("相机信息表未初始化或已释放");
  }
  if (id <= 0) {
    return DbResult<bool>::Error("无效的相机ID");
  }

  // ✅ 统一使用连接池
  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) {
    return DbResult<bool>::Error("数据库未打开");
  }
  qInfo() << "数据库连接正常";

  QMutexLocker locker(&m_ops->m_mutex);
  QSqlQuery query(c.db);  // ✅ 使用池连接而不是主连接
  query.prepare(DELETE_SQL);
  qInfo() << "SQL语句:" << DELETE_SQL;
  query.addBindValue(id);

  if (!query.exec()) {
    QString error = QString("删除相机失败: %1").arg(query.lastError().text());
    m_ops->logOperation("删除失败", error);
    emit m_ops->databaseError(error);
    return DbResult<bool>::Error(error);
  }

  if (query.numRowsAffected() == 0) {
    return DbResult<bool>::Error("未找到指定的相机记录");
  }

  m_ops->logOperation("删除成功", QString("相机ID: %1").arg(id));
  emit m_ops->recordDeleted(id);

  return DbResult<bool>::Success(true);
}

DbResult<CameraInfo> CameraInfoTable::selectById(int id) const {
  if (!m_ops) {
    return DbResult<CameraInfo>::Error("相机信息表未初始化或已释放");
  }

  if (id <= 0) {
    return DbResult<CameraInfo>::Error("无效的相机ID");
  }

  // ✅ 统一使用连接池
  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) {
    QString error = QString("数据库未打开");
    return DbResult<CameraInfo>::Error(error);
  }
  qInfo() << "数据库连接正常";

  QMutexLocker locker(&m_ops->m_mutex);
  QSqlQuery query(c.db);  // ✅ 使用池连接而不是主连接
  query.prepare(SELECT_BY_ID_SQL);
  qInfo() << "SQL语句:" << SELECT_BY_ID_SQL;
  query.addBindValue(id);

  if (!query.exec()) {
    QString error = QString("查询相机失败: %1").arg(query.lastError().text());
    return DbResult<CameraInfo>::Error(error);
  }

  if (query.next()) {
    CameraInfo camera = buildCameraInfo(query);
    return DbResult<CameraInfo>::Success(camera);
  }

  return DbResult<CameraInfo>::Error("未找到指定的相机记录");
}

DbResult<QList<CameraInfo>> CameraInfoTable::selectAll() const {
  if (!m_ops) {
    return DbResult<QList<CameraInfo>>::Error("相机信息表未初始化或已释放");
  }

  // ✅ 统一使用连接池
  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) {
    return DbResult<QList<CameraInfo>>::Error("数据库未打开");
  }
  qInfo() << "数据库连接正常";

  QMutexLocker locker(&m_ops->m_mutex);
  QSqlQuery query(c.db);  // ✅ 使用池连接而不是主连接

  if (!query.exec(SELECT_ALL_SQL)) {
    QString error =
        QString("查询所有相机失败: %1").arg(query.lastError().text());
    return DbResult<QList<CameraInfo>>::Error(error);
  }

  QList<CameraInfo> cameras;
  while (query.next()) {
    cameras.append(buildCameraInfo(query));
  }

  return DbResult<QList<CameraInfo>>::Success(cameras);
}

DbResult<PageResult<CameraInfo>> CameraInfoTable::selectByPage(
    const PageParams& params) const {
  if (!m_ops) {
    return DbResult<PageResult<CameraInfo>>::Error(
        "相机信息表未初始化或已释放");
  }
  auto c = m_ops->acquireDb();
  if (!c.db.isOpen())
    return DbResult<PageResult<CameraInfo>>::Error("数据库未打开");

  int total = m_ops->getTotalCount();
  QMutexLocker locker(&m_ops->m_mutex);

  QString orderBy =
      params.orderBy.isEmpty() ? "name" : sanitizeOrderBy(params.orderBy);
  QString sql = QString(
                    "SELECT "
                    "id,name,version,connection_type,serial_number,"
                    "manufacturer,created_at,updated_at "
                    "FROM camera_info ORDER BY %1 %2 LIMIT %3 OFFSET %4")
                    .arg(orderBy)
                    .arg(params.ascending ? "ASC" : "DESC")
                    .arg(params.pageSize)
                    .arg(params.offset());

  QSqlQuery query(c.db);
  if (!query.exec(sql)) {
    return DbResult<PageResult<CameraInfo>>::Error(
        QString("分页查询相机失败: %1").arg(query.lastError().text()));
  }

  QList<CameraInfo> list;
  while (query.next()) list.append(buildCameraInfo(query));
  return DbResult<PageResult<CameraInfo>>::Success(
      PageResult<CameraInfo>(list, total, params));
}

DbResult<int> CameraInfoTable::batchInsert(const QList<CameraInfo>& cameras) {
  if (!m_ops) {
    return DbResult<int>::Error("相机信息表未初始化或已释放");
  }
  if (cameras.isEmpty()) {
    return DbResult<int>::Error("相机列表为空");
  }

  // 1) 批内去重 + 基本校验（不持锁，不访问数据库）
  QList<CameraInfo> deduped;
  QSet<QString> seenSerials;
  QStringList errors;

  for (const CameraInfo& cam : cameras) {
    // 基本字段校验
    auto validation = validateCameraInfo(cam, false);
    if (!validation.success) {
      errors.append(
          QString("相机 '%1': %2").arg(cam.name, validation.errorMessage));
      continue;
    }

    // 批内去重：同一批次只保留首个相同序列号
    const QString sn = cam.serialNumber;
    if (seenSerials.contains(sn)) {
      errors.append(
          QString("序列号 '%1' 在同一批次内重复，已忽略后续重复项").arg(sn));
      continue;
    }
    seenSerials.insert(sn);
    deduped.append(cam);
  }

  if (deduped.isEmpty()) {
    return DbResult<int>::Error(
        QString("批量插入失败: %1").arg(errors.join("; ")));
  }

  // 2) 获取池连接
  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) {
    return DbResult<int>::Error("数据库未打开");
  }
  qInfo() << "数据库连接正常";

  // 3) 事务 + 批量插入（持锁）。与库内数据的冲突依赖 UNIQUE(serial_number)
  QMutexLocker locker(&m_ops->m_mutex);
  QSqlQuery query(c.db);
  query.prepare(INSERT_SQL);
  qInfo() << "SQL语句:" << INSERT_SQL;

  if (!c.db.transaction()) {
    return DbResult<int>::Error("无法开启事务");
  }

  int successCount = 0;
  QDateTime now = QDateTime::currentDateTime();

  for (const CameraInfo& cam : deduped) {
    // 用位置绑定，避免 addBindValue 在循环中的潜在累积
    query.bindValue(0, cam.name);
    query.bindValue(1, cam.version);
    query.bindValue(2, cam.connectionType);
    query.bindValue(3, cam.serialNumber);
    query.bindValue(4, cam.manufacturer);
    query.bindValue(5, now);
    query.bindValue(6, now);

    if (query.exec()) {
      successCount++;
      const int newId = query.lastInsertId().toInt();
      emit m_ops->recordInserted(newId);
    } else {
      // 依赖 UNIQUE 约束：若库里已有同序列号，这里会失败；我们收集错误并继续
      errors.append(QString("序列号 '%1' 插入失败: %2")
                        .arg(cam.serialNumber, query.lastError().text()));
    }
  }

  if (successCount > 0) {
    if (!c.db.commit()) {
      c.db.rollback();
      return DbResult<int>::Error("提交事务失败");
    }
    m_ops->logOperation("批量插入成功",
                        QString("成功插入 %1 个相机").arg(successCount));
    if (!errors.isEmpty()) {
      qWarning() << "部分插入失败:" << errors.join("; ");
    }
    return DbResult<int>::Success(successCount);
  } else {
    c.db.rollback();
    return DbResult<int>::Error(
        QString("批量插入失败: %1").arg(errors.join("; ")));
  }
}

DbResult<CameraInfo> CameraInfoTable::selectBySerialNumber(
    const QString& serialNumber) const {
  if (!m_ops) {
    return DbResult<CameraInfo>::Error("相机信息表未初始化或已释放");
  }

  if (serialNumber.isEmpty()) {
    return DbResult<CameraInfo>::Error("序列号不能为空");
  }

  if (!m_ops) {
    return DbResult<CameraInfo>::Error("相机信息表未初始化或已释放");
  }
  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) return DbResult<CameraInfo>::Error("数据库未打开");

  QMutexLocker locker(&m_ops->m_mutex);
  QSqlQuery query(c.db);
  query.prepare(SELECT_BY_SERIAL_SQL);
  query.addBindValue(serialNumber);

  if (!query.exec()) {
    QString error =
        QString("根据序列号查询失败: %1").arg(query.lastError().text());
    return DbResult<CameraInfo>::Error(error);
  }

  if (query.next()) {
    CameraInfo camera = buildCameraInfo(query);
    return DbResult<CameraInfo>::Success(camera);
  }

  return DbResult<CameraInfo>::Error("未找到指定序列号的相机");
}

bool CameraInfoTable::serialNumberExists(const QString& serialNumber,
                                         int excludeId) const {
  if (!m_ops) return false;
  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) return false;

  QMutexLocker locker(&m_ops->m_mutex);

  QSqlQuery query(c.db);
  query.prepare(CHECK_SERIAL_EXISTS_SQL);
  query.addBindValue(serialNumber);
  query.addBindValue(excludeId);

  if (query.exec() && query.next()) {
    return query.value(0).toInt() > 0;
  }

  return false;
}

DbResult<QList<CameraInfo>> CameraInfoTable::search(
    const QString& keyword) const {
  if (!m_ops) {
    return DbResult<QList<CameraInfo>>::Error("相机信息表未初始化或已释放");
  }

  if (keyword.isEmpty()) return selectAll();

  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) return DbResult<QList<CameraInfo>>::Error("数据库未打开");

  QMutexLocker locker(&m_ops->m_mutex);
  QSqlQuery query(c.db);
  query.prepare(SEARCH_SQL);

  const QString pattern = "%" + keyword + "%";  // ✅ 修正
  query.addBindValue(pattern);
  query.addBindValue(pattern);
  query.addBindValue(pattern);

  if (!query.exec()) {
    return DbResult<QList<CameraInfo>>::Error(
        QString("搜索相机失败: %1").arg(query.lastError().text()));
  }

  QList<CameraInfo> out;
  while (query.next()) out.append(buildCameraInfo(query));
  return DbResult<QList<CameraInfo>>::Success(out);
}

DbResult<QList<CameraInfo>> CameraInfoTable::selectByManufacturer(
    const QString& manufacturer) const {
  if (!m_ops) {
    return DbResult<QList<CameraInfo>>::Error("相机信息表未初始化或已释放");
  }

  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) return DbResult<QList<CameraInfo>>::Error("数据库未打开");

  QMutexLocker locker(&m_ops->m_mutex);

  QString sql = R"(
        SELECT id, name, version, connection_type, serial_number, manufacturer, created_at, updated_at
        FROM camera_info WHERE manufacturer = ? ORDER BY name
    )";

  QSqlQuery query(c.db);
  query.prepare(sql);
  query.addBindValue(manufacturer);

  if (!query.exec()) {
    QString error =
        QString("根据制造商查询失败: %1").arg(query.lastError().text());
    return DbResult<QList<CameraInfo>>::Error(error);
  }

  QList<CameraInfo> cameras;
  while (query.next()) {
    cameras.append(buildCameraInfo(query));
  }

  return DbResult<QList<CameraInfo>>::Success(cameras);
}

QStringList CameraInfoTable::getAllManufacturers() const {
  if (!m_ops) return {};
  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) return {};

  QMutexLocker locker(&m_ops->m_mutex);

  QString sql =
      "SELECT DISTINCT manufacturer FROM camera_info WHERE manufacturer IS NOT "
      "NULL ORDER BY manufacturer";

  QSqlQuery query(c.db);
  if (!query.exec(sql)) {
    return QStringList();
  }

  QStringList manufacturers;
  while (query.next()) {
    QString manufacturer = query.value(0).toString();
    if (!manufacturer.isEmpty()) {
      manufacturers.append(manufacturer);
    }
  }

  return manufacturers;
}

DbResult<QList<CameraInfo>> CameraInfoTable::selectByConnectionType(
    const QString& connectionType) const {
  if (!m_ops)
    return DbResult<QList<CameraInfo>>::Error("相机信息表未初始化或已释放");
  auto c = m_ops->acquireDb();
  if (!c.db.isOpen()) return DbResult<QList<CameraInfo>>::Error("数据库未打开");

  QMutexLocker locker(&m_ops->m_mutex);

  QString sql = R"(
        SELECT id, name, version, connection_type, serial_number, manufacturer, created_at, updated_at
        FROM camera_info WHERE connection_type = ? ORDER BY name
    )";

  QSqlQuery query(c.db);
  query.prepare(sql);
  query.addBindValue(connectionType);

  if (!query.exec()) {
    QString error =
        QString("根据连接类型查询失败: %1").arg(query.lastError().text());
    return DbResult<QList<CameraInfo>>::Error(error);
  }

  QList<CameraInfo> cameras;
  while (query.next()) {
    cameras.append(buildCameraInfo(query));
  }

  return DbResult<QList<CameraInfo>>::Success(cameras);
}

CameraInfo CameraInfoTable::buildCameraInfo(const QSqlQuery& query) const {
  CameraInfo camera;

  camera.id = query.value(0).toInt();
  camera.name = query.value(1).toString();
  camera.version = query.value(2).toString();
  camera.connectionType = query.value(3).toString();
  camera.serialNumber = query.value(4).toString();
  camera.manufacturer = query.value(5).toString();
  camera.createdAt = query.value(6).toDateTime();
  camera.updatedAt = query.value(7).toDateTime();

  return camera;
}

// 在 DeviceDatabaseManager.cpp 的 validateCameraInfo 方法中修改序列号验证

DbResult<bool> CameraInfoTable::validateCameraInfo(const CameraInfo& camera,
                                                   bool isUpdate) const {
  // 检查必填字段
  if (camera.name.isEmpty()) {
    return DbResult<bool>::Error("相机名称不能为空");
  }

  if (camera.serialNumber.isEmpty()) {
    return DbResult<bool>::Error("序列号不能为空");
  }

  // 检查字段长度
  if (camera.name.length() > 255) {
    return DbResult<bool>::Error("相机名称长度不能超过255个字符");
  }

  if (camera.serialNumber.length() > 100) {
    return DbResult<bool>::Error("序列号长度不能超过100个字符");
  }

  if (!camera.version.isEmpty() && camera.version.length() > 50) {
    return DbResult<bool>::Error("版本号长度不能超过50个字符");
  }

  if (!camera.connectionType.isEmpty() && camera.connectionType.length() > 50) {
    return DbResult<bool>::Error("连接类型长度不能超过50个字符");
  }

  if (!camera.manufacturer.isEmpty() && camera.manufacturer.length() > 255) {
    return DbResult<bool>::Error("制造商名称长度不能超过255个字符");
  }

  // 序列号格式验证 - 更宽松的验证，只检查是否包含可打印字符
  // 可以根据实际需求调整这个验证规则
  for (const QChar& ch : camera.serialNumber) {
    if (!ch.isPrint() || ch.isSpace()) {
      return DbResult<bool>::Error("序列号不能包含空白字符或不可打印字符");
    }
  }

  // 如果需要严格的格式验证，可以启用下面的代码：
  // if (!serialRegex.match(camera.serialNumber).hasMatch()) {
  //   return DbResult<bool>::Error(
  //       QString("序列号格式无效: '%1'，当前序列号: '%2'")
  //           .arg("只能包含字母、数字、下划线、短划线和花括号")
  //           .arg(camera.serialNumber));
  // }

  return DbResult<bool>::Success(true);
}
