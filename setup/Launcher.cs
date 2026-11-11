using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;
internal static class Launcher {
    [STAThread] static int Main() {
        try {
            string game=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"Game");
            if(File.Exists(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"INSTALLATION-INCOMPLETE.txt")) || !File.Exists(Path.Combine(game,"installation-complete.txt"))) throw new IOException("Setup has not finished. Run the latest installer into a new folder.");
            if(!File.Exists(Path.Combine(game,"assets","default.xex"))) throw new IOException("Game data is missing. Run Setup OutRun Online Arcade.exe to import your Xbox 360 package.");
            Process.Start(new ProcessStartInfo(Path.Combine(game,"outrun.exe")) {WorkingDirectory=game,UseShellExecute=false});
            return 0;
        } catch(Exception ex) {MessageBox.Show(ex.Message,"OutRun Online Arcade PC",MessageBoxButtons.OK,MessageBoxIcon.Error); return 1;}
    }
}
