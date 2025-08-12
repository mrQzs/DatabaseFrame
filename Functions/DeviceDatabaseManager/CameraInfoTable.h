#ifndef CAMERAINFOTABLE_H
#define CAMERAINFOTABLE_H

#include <QPointer>

#include "BaseDatabaseManager.h"
#include "DeviceDataBaseStruct.h"

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
  struct TxGuard {
    QSqlDatabase& db;
    bool active = false;
    explicit TxGuard(QSqlDatabase& d) : db(d) { active = db.transaction(); }
    ~TxGuard() {
      if (active) db.rollback();
    }
    bool commit() {
      if (!active) return false;
      bool ok = db.commit();
      active = false;
      return ok;
    }
  };

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

  QPointer<CameraInfoTableOperations> m_ops;  ///< 安全弱引用，避免悬空
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
  CameraInfoTableOperations* operations() const { return m_ops.data(); }

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

#endif  // CAMERAINFOTABLE_H
