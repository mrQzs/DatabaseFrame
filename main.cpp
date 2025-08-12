// main.cpp (简化版本)
#include <QCoreApplication>
#include <QTextCodec>

#include "DatabaseTestExample.h"

#ifdef _WIN32
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#endif

void setupConsoleEncoding() {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif
}

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);

  // 设置应用程序信息
  app.setApplicationName("DatabaseFrameworkTest");
  app.setApplicationVersion("1.0.0");
  app.setOrganizationName("Framework Test Corp");

  // 设置编码
  QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
  setupConsoleEncoding();

  qInfo() << "数据库框架测试程序启动";
  qInfo() << "应用程序:" << app.applicationName() << app.applicationVersion();
  qInfo() << "Qt版本:" << QT_VERSION_STR;

  // 创建并运行测试
  DatabaseTestExample testExample;
  testExample.runAllTests();

  return app.exec();
}
