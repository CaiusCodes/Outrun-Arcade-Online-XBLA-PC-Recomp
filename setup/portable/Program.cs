using System;
using System.IO;
using System.Windows.Forms;

namespace Outrun.Portable {
    internal static class Program {
        [STAThread] static int Main(string[] args) {
            try {
                AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling",false);AppContext.SetSwitch("Switch.System.IO.BlockLongPaths",false);
                Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
                var game=new Outrun.Setup.PortableGame(AppDomain.CurrentDomain.BaseDirectory);
                if(args.Length>=2&&args[0]=="--render-preview") {
                    using(var form=new Outrun.Setup.ImportForm(game,null,true)){form.Preview(args.Length>2?args[2]:"ready");Outrun.Setup.SetupPreview.Render(form,Path.GetFullPath(args[1]),args.Length>3?Int32.Parse(args[3]):0);}return 0;
                }
                // An update cut off half-way is undone (or finished) before anything else.
                Outrun.Setup.Updates.RecoverInterrupted(game.Root);
                // --update: the game's Help & Options > Check for Updates > Update.
                // One update window per folder; it starts the game again afterwards.
                if(args.Length==1&&args[0]=="--update") {
                    using(var mutex=new System.Threading.Mutex(false,Outrun.Setup.Updates.MutexName(game.Root))) {
                        bool owned;try{owned=mutex.WaitOne(0);}catch(System.Threading.AbandonedMutexException){owned=true;}
                        if(!owned)return 0;
                        try {
                            game.Recover();
                            Outrun.Setup.Updates.Log(game.Root,"Update window opened (this copy v"+(Outrun.Setup.Updates.InstalledVersion(game.Resources)??"unknown")+")");
                            using(var form=new Outrun.Setup.UpdateForm(game)){Application.Run(form);return form.Succeeded?0:1;}
                        }finally{mutex.ReleaseMutex();}
                    }
                }
                game.Recover();
                bool manage=(args.Length==1&&args[0]=="--manage")||(Control.ModifierKeys&Keys.Shift)!=0;
                string input=null;if(args.Length>0&&!manage){if(args.Length!=1||!(File.Exists(args[0])||Directory.Exists(args[0])))throw new ArgumentException("Open OutRun, drop a package/folder on it, or use --manage.");input=args[0];}
                if(!manage&&input==null&&game.Ready()){game.Launch();return 0;}
                Application.Run(new Outrun.Setup.ImportForm(game,input));return 0;
            }catch(Exception ex){
                if(args.Length>1&&args[0]=="--render-preview"){File.WriteAllText(args[1]+".error.txt",ex.ToString());return 1;}
                MessageBox.Show(ex.Message,"OutRun Online Arcade",MessageBoxButtons.OK,MessageBoxIcon.Error);return 1;
            }
        }
    }
}
