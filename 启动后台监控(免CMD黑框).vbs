Set ws = CreateObject("WScript.Shell")
ws.CurrentDirectory = "D:\PYTHON\Ai passport"
cmd = Chr(34) & "C:\Users\Admin\AppData\Local\Python\pythoncore-3.14-64\python.exe" & Chr(34) & " tools\silent_bridge.py"
ws.Run cmd, 0, False
Set ws = Nothing
