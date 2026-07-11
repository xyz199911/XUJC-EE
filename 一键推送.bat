@echo off
chcp 65001 >nul
cd /d D:\study-gh
echo.
echo === 添加文件 ===
git add .
echo.
set /p msg=请输入说明：
git commit -m "%msg%"
echo.
echo === 推送中 ===
git push
echo.
echo ✅ 完成！
pause