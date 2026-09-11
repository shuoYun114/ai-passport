' ========================================================
' AI Passport 后台静默启动器 (无任何 CMD 黑框)
' ========================================================
Set ws = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

' 获取当前目录路径
currentDir = fso.GetParentFolderName(WScript.ScriptFullName)
pythonScript = currentDir & "\tools\silent_bridge.py"

' 使用 pythonw.exe 执行（0 表示完全隐藏窗口，False 表示后台异步运行）
command = "pythonw.exe """ & pythonScript & """"
ws.Run command, 0, False

Set ws = Nothing
Set fso = Nothing
