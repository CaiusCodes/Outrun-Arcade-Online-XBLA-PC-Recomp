using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace Outrun.Setup {
    internal static class Program {
        [STAThread] public static int Main(string[] args) {
            if (args.Length == 3 && args[0] == "--install") {
                try { InstallEngine.Install(args[1], args[2], (p,s) => {}); return 0; }
                catch (Exception ex) { try { File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "setup-error-" + Guid.NewGuid().ToString("N") + ".txt"), ex.ToString()); } catch {} return 1; }
            }
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            if (args.Length >= 2 && args[0] == "--render-preview") {
                try {using (var form = new SetupForm()) {
                    form.Preview(args.Length > 2 ? args[2] : "ready");
                    SetupPreview.Render(form,Path.GetFullPath(args[1]),args.Length>3?Int32.Parse(args[3]):0);
                }}catch(Exception ex){File.WriteAllText(args[1]+".error.txt",ex.ToString());return 1;}
                return 0;
            }
            Application.Run(new SetupForm());
            return 0;
        }
    }
}
