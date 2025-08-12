// DatabaseTestExample.h
#ifndef DATABASE_TEST_EXAMPLE_H
#define DATABASE_TEST_EXAMPLE_H

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QTextCodec>
#include <QTimer>
#include <thread>

#include "DatabaseRegistry.h"

#ifdef _WIN32
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#endif

/**
 * @brief 数据库测试类
 * 展示如何使用新的数据库框架
 */
class DatabaseTestExample : public QObject {
  Q_OBJECT

 private:
  DatabaseRegistry* m_registry;
  int m_testsPassed = 0;
  int m_testsFailed = 0;

 public:
  explicit DatabaseTestExample(QObject* parent = nullptr) : QObject(parent) {
    m_registry = DatabaseRegistry::getInstance();

    // 连接注册中心信号
    connect(m_registry, &DatabaseRegistry::initializationCompleted, this,
            &DatabaseTestExample::onRegistryInitialized);
    connect(m_registry, &DatabaseRegistry::databaseError, this,
            &DatabaseTestExample::onDatabaseError);
  }

  /**
   * @brief 运行所有测试
   */
  void runAllTests() {
    qInfo() << "\n========================================";
    qInfo() << "    数据库框架测试开始";
    qInfo() << "========================================\n";

    // 初始化数据库注册中心
    if (!m_registry->initialize("./test_framework_db")) {
      qCritical() << "数据库注册中心初始化失败";
      return;
    }

    // 等待初始化完成后再开始测试
    QTimer::singleShot(100, this, &DatabaseTestExample::startTests);
  }

 private slots:
  /**
   * @brief 注册中心初始化完成处理
   */
  void onRegistryInitialized(bool success, const QString& message) {
    qInfo() << "注册中心初始化结果:" << (success ? "成功" : "失败") << "-"
            << message;
  }

  /**
   * @brief 数据库错误处理
   */
  void onDatabaseError(DatabaseType dbType, const QString& error) {
    qWarning() << QString("数据库错误 [%1]: %2")
                      .arg(static_cast<int>(dbType))
                      .arg(error);
  }

  /**
   * @brief 开始执行测试
   */
  void startTests() {
    qInfo() << "开始执行数据库测试...\n";

    // 基础功能测试
    testDatabaseRegistry();
    testDeviceDatabaseBasicOperations();
    testCameraInfoCRUD();
    testCameraInfoAdvancedQueries();
    testBatchOperations();
    testTransactionOperations();
    testDatabaseMaintenance();
    testPerformance();
    testConcurrency();

    // 输出测试结果
    printTestResults();

    // 清理并退出
    cleanup();
  }

  /**
   * @brief 清理资源
   */
  void cleanup() {
    qInfo() << "\n清理测试环境...";
    m_registry->shutdown();
    DatabaseRegistry::destroyInstance();

    QTimer::singleShot(500, []() { QCoreApplication::quit(); });
  }

 private:
  /**
   * @brief 测试断言
   */
  void TEST_ASSERT(bool condition, const QString& testName,
                   const QString& message = "") {
    if (condition) {
      m_testsPassed++;
      qInfo() << "✓ PASS:" << testName;
    } else {
      m_testsFailed++;
      qCritical() << "✗ FAIL:" << testName;
      if (!message.isEmpty()) {
        qCritical() << "  原因:" << message;
      }
    }
  }

  /**
   * @brief 创建测试相机信息
   */
  CameraInfo createTestCamera(const QString& suffix = "") {
    CameraInfo camera;
    camera.name = "Framework Test Camera" + suffix;
    camera.version = "v2.0.0" + suffix;
    camera.connectionType = "USB-C";
    // 改这里：去掉花括号
    QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    camera.serialNumber = "FTC_" + uuid.left(8) + suffix;
    camera.manufacturer = "Framework Test Corp" + suffix;
    return camera;
  }

  /**
   * @brief 测试数据库注册中心
   */
  void testDatabaseRegistry() {
    qInfo() << "\n[测试数据库注册中心]";

    TEST_ASSERT(m_registry != nullptr, "获取注册中心实例");
    TEST_ASSERT(m_registry->isInitialized(), "验证注册中心已初始化");

    // 测试单例模式
    DatabaseRegistry* instance2 = DatabaseRegistry::getInstance();
    TEST_ASSERT(m_registry == instance2, "验证单例模式");

    // 测试数据库可用性
    TEST_ASSERT(m_registry->isDatabaseAvailable(DatabaseType::DEVICE_DB),
                "设备数据库可用");

    // 测试数据库访问
    DeviceDatabaseManager* deviceDb = m_registry->deviceDatabase();
    TEST_ASSERT(deviceDb != nullptr, "获取设备数据库管理器");
    TEST_ASSERT(deviceDb->isOpen(), "设备数据库已打开");
  }

  /**
   * @brief 测试设备数据库基本操作
   */
  void testDeviceDatabaseBasicOperations() {
    qInfo() << "\n[测试设备数据库基本操作]";

    DeviceDatabaseManager* deviceDb = DEVICE_DB();
    TEST_ASSERT(deviceDb != nullptr, "获取设备数据库");

    // 测试表访问
    CameraInfoTable* cameraTable = deviceDb->cameraInfoTable();
    TEST_ASSERT(cameraTable != nullptr, "获取相机信息表");
    TEST_ASSERT(cameraTable->operations()->tableExists(), "相机信息表存在");

    // 测试表基本信息
    QString tableName = cameraTable->operations()->tableName();
    TEST_ASSERT(tableName == "camera_info", "验证表名");

    TableType tableType = cameraTable->operations()->tableType();
    TEST_ASSERT(tableType == TableType::CAMERA_INFO, "验证表类型");
  }

  /**
   * @brief 测试相机信息CRUD操作
   */
  void testCameraInfoCRUD() {
    qInfo() << "\n[测试相机信息CRUD操作]";

    DeviceDatabaseManager* deviceDb = DEVICE_DB();

    // 清空表数据
    deviceDb->cameraInfoTable()->operations()->truncateTable();

    // 测试创建
    CameraInfo camera = createTestCamera("_crud");
    auto addResult = deviceDb->addCamera(camera);
    TEST_ASSERT(addResult.success, "添加相机");
    TEST_ASSERT(addResult.data > 0, "验证返回的ID有效");

    int cameraId = addResult.data;

    // 测试读取
    auto getResult = deviceDb->getCamera(cameraId);
    TEST_ASSERT(getResult.success, "根据ID获取相机");
    TEST_ASSERT(getResult.data.name == camera.name, "验证相机名称");
    TEST_ASSERT(getResult.data.serialNumber == camera.serialNumber,
                "验证序列号");

    // 测试根据序列号获取
    auto getBySerialResult =
        deviceDb->getCameraBySerialNumber(camera.serialNumber);
    TEST_ASSERT(getBySerialResult.success, "根据序列号获取相机");
    TEST_ASSERT(getBySerialResult.data.id == cameraId, "验证ID匹配");

    // 测试更新
    CameraInfo updatedCamera = getResult.data;
    updatedCamera.name = "Updated Framework Camera";
    updatedCamera.version = "v3.0.0";
    auto updateResult = deviceDb->updateCamera(updatedCamera);
    TEST_ASSERT(updateResult.success, "更新相机信息");

    // 验证更新
    auto getUpdatedResult = deviceDb->getCamera(cameraId);
    TEST_ASSERT(getUpdatedResult.success, "获取更新后的相机");
    TEST_ASSERT(getUpdatedResult.data.name == "Updated Framework Camera",
                "验证名称更新");
    TEST_ASSERT(getUpdatedResult.data.version == "v3.0.0", "验证版本更新");

    // 测试删除
    auto deleteResult = deviceDb->removeCamera(cameraId);
    TEST_ASSERT(deleteResult.success, "删除相机");

    // 验证删除
    auto getDeletedResult = deviceDb->getCamera(cameraId);
    TEST_ASSERT(!getDeletedResult.success, "验证相机已被删除");
  }

  /**
   * @brief 测试相机信息高级查询
   */
  void testCameraInfoAdvancedQueries() {
    qInfo() << "\n[测试相机信息高级查询]";

    DeviceDatabaseManager* deviceDb = DEVICE_DB();
    CameraInfoTable* cameraTable = deviceDb->cameraInfoTable();

    // 清空表数据
    cameraTable->operations()->truncateTable();

    // 添加测试数据
    QList<CameraInfo> testCameras;

    CameraInfo sony1 = createTestCamera("_sony1");
    sony1.name = "Sony Alpha A7R IV";
    sony1.manufacturer = "Sony Corporation";
    sony1.connectionType = "USB-C";
    testCameras.append(sony1);

    CameraInfo sony2 = createTestCamera("_sony2");
    sony2.name = "Sony FX6";
    sony2.manufacturer = "Sony Corporation";
    sony2.connectionType = "Ethernet";
    testCameras.append(sony2);

    CameraInfo canon = createTestCamera("_canon");
    canon.name = "Canon EOS R5";
    canon.manufacturer = "Canon Inc.";
    canon.connectionType = "USB-C";
    testCameras.append(canon);

    // 批量插入
    auto batchResult = cameraTable->batchInsert(testCameras);
    TEST_ASSERT(batchResult.success, "批量插入相机");
    TEST_ASSERT(batchResult.data == 3, "验证插入数量");

    // 测试获取所有相机
    auto allResult = deviceDb->getAllCameras();
    TEST_ASSERT(allResult.success, "获取所有相机");
    TEST_ASSERT(allResult.data.size() == 3, "验证相机数量");

    // 测试搜索功能
    auto searchSonyResult = deviceDb->searchCameras("Sony");
    TEST_ASSERT(searchSonyResult.success, "搜索Sony相机");
    TEST_ASSERT(searchSonyResult.data.size() == 2, "验证找到2个Sony相机");

    auto searchCanonResult = deviceDb->searchCameras("Canon");
    TEST_ASSERT(searchCanonResult.success, "搜索Canon相机");
    TEST_ASSERT(searchCanonResult.data.size() == 1, "验证找到1个Canon相机");

    // 测试根据制造商查询
    auto sonyByMfrResult =
        cameraTable->selectByManufacturer("Sony Corporation");
    TEST_ASSERT(sonyByMfrResult.success, "根据制造商查询Sony");
    TEST_ASSERT(sonyByMfrResult.data.size() == 2, "验证Sony制造商相机数量");

    // 测试根据连接类型查询
    auto usbCResult = cameraTable->selectByConnectionType("USB-C");
    TEST_ASSERT(usbCResult.success, "根据连接类型查询USB-C");
    TEST_ASSERT(usbCResult.data.size() == 2, "验证USB-C连接类型相机数量");

    // 测试获取制造商列表
    QStringList manufacturers = cameraTable->getAllManufacturers();
    TEST_ASSERT(manufacturers.size() == 2, "验证制造商数量");
    TEST_ASSERT(manufacturers.contains("Sony Corporation"), "包含Sony制造商");
    TEST_ASSERT(manufacturers.contains("Canon Inc."), "包含Canon制造商");

    // 测试分页查询
    PageParams pageParams;
    pageParams.pageIndex = 1;
    pageParams.pageSize = 2;
    pageParams.orderBy = "name";
    pageParams.ascending = true;

    auto pageResult = cameraTable->selectByPage(pageParams);
    TEST_ASSERT(pageResult.success, "分页查询");
    TEST_ASSERT(pageResult.data.data.size() == 2, "验证页面数据数量");
    TEST_ASSERT(pageResult.data.totalCount == 3, "验证总记录数");
    TEST_ASSERT(pageResult.data.totalPages == 2, "验证总页数");
  }

  /**
   * @brief 测试批量操作
   */
  void testBatchOperations() {
    qInfo() << "\n[测试批量操作]";

    DeviceDatabaseManager* deviceDb = DEVICE_DB();

    // 清空表数据
    deviceDb->cameraInfoTable()->operations()->truncateTable();

    // 准备批量数据
    QList<CameraInfo> cameras;
    const int batchSize = 10;

    for (int i = 0; i < batchSize; ++i) {
      cameras.append(createTestCamera(QString("_batch_%1").arg(i)));
    }

    // 添加一个无效的相机
    CameraInfo invalidCamera;
    invalidCamera.name = "Invalid Camera";
    // 缺少序列号，应该被拒绝
    cameras.append(invalidCamera);

    // 执行批量导入
    auto importResult = deviceDb->importCameras(cameras);
    TEST_ASSERT(importResult.success, "批量导入相机");
    TEST_ASSERT(importResult.data == batchSize, "验证成功导入数量");

    // 验证数据库中的记录数
    int totalCount = deviceDb->cameraInfoTable()->operations()->getTotalCount();
    TEST_ASSERT(totalCount == batchSize, "验证数据库中相机数量");

    // 测试重复序列号的处理
    QList<CameraInfo> duplicateCameras;
    CameraInfo duplicateCamera = cameras[0];  // 使用已存在的相机
    duplicateCamera.id = -1;                  // 重置ID
    duplicateCameras.append(duplicateCamera);

    auto duplicateResult = deviceDb->importCameras(duplicateCameras);
    TEST_ASSERT(!duplicateResult.success, "拒绝重复序列号");
  }

  /**
   * @brief 测试事务操作
   */
  void testTransactionOperations() {
    qInfo() << "\n[测试事务操作]";

    DeviceDatabaseManager* deviceDb = DEVICE_DB();

    // 清空表数据
    deviceDb->cameraInfoTable()->operations()->truncateTable();

    // 测试事务回滚
    bool beginResult = deviceDb->beginTransaction();
    TEST_ASSERT(beginResult, "开始事务");

    CameraInfo camera1 = createTestCamera("_trans1");
    auto addResult1 = deviceDb->addCamera(camera1);
    TEST_ASSERT(addResult1.success, "事务中添加相机");

    bool rollbackResult = deviceDb->rollbackTransaction();
    TEST_ASSERT(rollbackResult, "回滚事务");

    int count1 = deviceDb->cameraInfoTable()->operations()->getTotalCount();
    TEST_ASSERT(count1 == 0, "验证回滚后相机数为0");

    // 测试事务提交
    deviceDb->beginTransaction();

    CameraInfo camera2 = createTestCamera("_trans2");
    auto addResult2 = deviceDb->addCamera(camera2);
    TEST_ASSERT(addResult2.success, "事务中添加第二个相机");

    bool commitResult = deviceDb->commitTransaction();
    TEST_ASSERT(commitResult, "提交事务");

    int count2 = deviceDb->cameraInfoTable()->operations()->getTotalCount();
    TEST_ASSERT(count2 == 1, "验证提交后相机数为1");

    // 测试自动事务执行器
    auto transactionResult = deviceDb->executeInTransaction([&]() -> bool {
      CameraInfo camera3 = createTestCamera("_trans3");
      auto result = deviceDb->addCamera(camera3);
      return result.success;
    });

    TEST_ASSERT(transactionResult, "自动事务执行成功");

    int count3 = deviceDb->cameraInfoTable()->operations()->getTotalCount();
    TEST_ASSERT(count3 == 2, "验证自动事务后相机数为2");
  }

  /**
   * @brief 测试数据库维护功能
   */
  void testDatabaseMaintenance() {
    qInfo() << "\n[测试数据库维护功能]";

    // 测试健康检查
    auto healthStatus = m_registry->getDatabaseHealthStatus();
    TEST_ASSERT(!healthStatus.isEmpty(), "获取健康状态");
    TEST_ASSERT(healthStatus[DatabaseType::DEVICE_DB], "设备数据库健康");

    // 测试数据库优化
    auto optimizeResult = m_registry->optimizeAllDatabases();
    TEST_ASSERT(optimizeResult.success, "优化所有数据库");
    TEST_ASSERT(optimizeResult.data > 0, "至少优化了一个数据库");

    // 测试备份
    QString backupDir = "./test_backup";
    QDir().mkpath(backupDir);

    auto backupResult = m_registry->backupAllDatabases(backupDir);
    TEST_ASSERT(backupResult.success, "备份所有数据库");
    TEST_ASSERT(backupResult.data > 0, "至少备份了一个数据库");

    // 验证备份文件存在
    QDir backupDirObj(backupDir);
    QStringList backupFiles =
        backupDirObj.entryList(QStringList() << "*.db", QDir::Files);
    TEST_ASSERT(!backupFiles.isEmpty(), "备份文件已创建");

    // 测试统计信息
    auto allStats = m_registry->getAllDatabaseStats();
    TEST_ASSERT(!allStats.isEmpty(), "获取统计信息");

    if (allStats.contains(DatabaseType::DEVICE_DB)) {
      auto deviceStats = allStats[DatabaseType::DEVICE_DB];
      TEST_ASSERT(deviceStats.totalQueries > 0, "设备数据库有查询统计");
    }
  }

  /**
   * @brief 测试性能
   */
  void testPerformance() {
    qInfo() << "\n[测试性能]";

    DeviceDatabaseManager* deviceDb = DEVICE_DB();

    // 清空表数据
    deviceDb->cameraInfoTable()->operations()->truncateTable();

    QElapsedTimer timer;
    const int testCount = 100;

    // 测试插入性能
    timer.start();
    for (int i = 0; i < testCount; ++i) {
      CameraInfo camera = createTestCamera(QString("_perf_%1").arg(i));
      deviceDb->addCamera(camera);
    }
    qint64 insertTime = timer.elapsed();

    TEST_ASSERT(
        deviceDb->cameraInfoTable()->operations()->getTotalCount() == testCount,
        QString("插入%1个相机").arg(testCount));
    qInfo() << QString("  插入%1个相机耗时: %2ms (平均: %3ms)")
                   .arg(testCount)
                   .arg(insertTime)
                   .arg(insertTime / testCount);

    // 测试查询性能
    timer.restart();
    auto allCameras = deviceDb->getAllCameras();
    qint64 queryTime = timer.elapsed();

    TEST_ASSERT(allCameras.data.size() == testCount, "查询所有相机");
    qInfo()
        << QString("  查询%1个相机耗时: %2ms").arg(testCount).arg(queryTime);

    // 测试搜索性能
    timer.restart();
    auto searchResult = deviceDb->searchCameras("perf");
    qint64 searchTime = timer.elapsed();

    TEST_ASSERT(searchResult.success, "搜索相机");
    qInfo() << QString("  搜索耗时: %1ms (找到%2个结果)")
                   .arg(searchTime)
                   .arg(searchResult.data.size());

    // 性能基准检查
    TEST_ASSERT(insertTime / testCount < 10, "平均插入时间小于10ms");
    TEST_ASSERT(queryTime < 100, "查询时间小于100ms");
    TEST_ASSERT(searchTime < 50, "搜索时间小于50ms");
  }

  /**
   * @brief 测试并发性
   */
  void testConcurrency() {
    qInfo() << "\n[测试并发性]";

    DeviceDatabaseManager* deviceDb = DEVICE_DB();

    // 清空表数据
    deviceDb->cameraInfoTable()->operations()->truncateTable();

    const int threadCount = 3;
    const int operationsPerThread = 10;
    std::vector<std::thread> threads;
    std::atomic<int> successCount(0);
    std::atomic<int> errorCount(0);

    // 启动多个线程同时操作数据库
    for (int i = 0; i < threadCount; ++i) {
      threads.emplace_back([=, &successCount, &errorCount]() {
        for (int j = 0; j < operationsPerThread; ++j) {
          CameraInfo camera =
              createTestCamera(QString("_concurrent_%1_%2").arg(i).arg(j));
          auto result = deviceDb->addCamera(camera);

          if (result.success) {
            successCount++;
          } else {
            errorCount++;
          }
        }
      });
    }

    // 等待所有线程完成
    for (auto& thread : threads) {
      thread.join();
    }

    int totalOperations = threadCount * operationsPerThread;
    TEST_ASSERT(successCount + errorCount == totalOperations, "所有操作已完成");
    TEST_ASSERT(successCount > 0, "至少有成功的操作");

    qInfo() << QString("  并发操作结果: %1 成功, %2 失败")
                   .arg(successCount.load())
                   .arg(errorCount.load());

    // 验证数据库状态
    int finalCount = deviceDb->cameraInfoTable()->operations()->getTotalCount();
    TEST_ASSERT(finalCount == successCount, "数据库记录数与成功操作数匹配");
  }

  /**
   * @brief 输出测试结果
   */
  void printTestResults() {
    qInfo() << "\n========================================";
    qInfo() << "       测试结果汇总";
    qInfo() << "========================================";
    qInfo() << "通过测试:" << m_testsPassed;
    qInfo() << "失败测试:" << m_testsFailed;
    qInfo() << "总计测试:" << (m_testsPassed + m_testsFailed);

    if (m_testsPassed + m_testsFailed > 0) {
      double successRate =
          m_testsPassed * 100.0 / (m_testsPassed + m_testsFailed);
      qInfo() << "成功率:" << QString("%1%").arg(successRate, 0, 'f', 1);
    }

    qInfo() << "========================================\n";

    if (m_testsFailed == 0) {
      qInfo() << "🎉 所有测试通过！数据库框架工作正常。";
    } else {
      qWarning() << "⚠️  有测试失败，请检查数据库框架实现。";
    }
  }
};

#endif  // DATABASE_TEST_EXAMPLE_H
