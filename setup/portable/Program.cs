using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Windows.Forms;

namespace Outrun.Portable {
    internal static class Program {
        [STAThread] static int Main(string[] args) {
            bool windowless=args.Length>0&&(args[0]=="--prepare"||args[0]=="--play");
            try {
                AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling",false);AppContext.SetSwitch("Switch.System.IO.BlockLongPaths",false);
                if(!windowless){Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);}
                var game=new Outrun.Setup.PortableGame(AppDomain.CurrentDomain.BaseDirectory);
                if(args.Length>=2&&args[0]=="--render-preview") {
                    using(var form=new Outrun.Setup.ImportForm(game,null,true)){form.Preview(args.Length>2?args[2]:"ready");Outrun.Setup.SetupPreview.Render(form,Path.GetFullPath(args[1]),args.Length>3?Int32.Parse(args[3]):0);}return 0;
                }
                // An update cut off half-way is undone (or finished) before anything else.
                Outrun.Setup.Updates.RecoverInterrupted(game.Root);
                // --prepare / --play: RecompLauncher (recomplauncher.json). No window.
                if(windowless&&args[0]=="--prepare")return LauncherCli.Prepare(game,LauncherCli.GameArgument(args));
                if(windowless&&args[0]=="--play")return LauncherCli.Play(game);
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
                if(windowless){Console.Error.WriteLine(ex.Message);return 1;}
                if(args.Length>1&&args[0]=="--render-preview"){File.WriteAllText(args[1]+".error.txt",ex.ToString());return 1;}
                MessageBox.Show(ex.Message,"OutRun Online Arcade",MessageBoxButtons.OK,MessageBoxIcon.Error);return 1;
            }
        }
    }
    // RecompLauncher support (recomplauncher.json). Used only when the launcher
    // passes --prepare or --play; without them nothing here runs.
    //   --prepare --game <package file or folder>: imports the game files like
    //     the setup window, printing "progress <0..1> <text>" lines. Exit 0 = ready.
    //   --play: starts the game and waits until it closes (following Restart Now),
    //     returning the game's exit code, so the launcher sees the whole session.
    internal static class LauncherCli {
        internal const int NotImported=75,RuntimeMissing=76;
        internal static string GameArgument(string[] args) {
            for(int i=1;i+1<args.Length;i++)if(args[i]=="--game")return args[i+1];
            return null;
        }
        static void Progress(double value,string text) {
            Console.Out.WriteLine("progress "+Math.Max(0,Math.Min(1,value)).ToString("0.###",CultureInfo.InvariantCulture)+" "+text);Console.Out.Flush();
        }
        internal static int Prepare(Outrun.Setup.PortableGame game,string input) {
            if(input==null||!(File.Exists(input)||Directory.Exists(input))){Console.Error.WriteLine("Pass --game with your OutRun Online Arcade package file or its folder.");return 2;}
            game.Recover();
            Progress(0,"Checking your copy");
            if(game.Ready()) {
                // Only one revision is supported, so the files already imported stay;
                // a package is still checked so a wrong pick is reported.
                if(File.Exists(input))Outrun.Setup.PortableGame.ValidatePackage(input);
                Progress(1,"Game files already imported");return 0;
            }
            int last=-1;
            Action<int,string> report=(p,s)=>{if(p==last)return;last=p;Progress(p/100.0,s.TrimEnd('…','.'));};
            if(File.Exists(input))game.Import(input,report,CancellationToken.None);
            else if(game.LegacyResources(input)!=null)game.Migrate(input,report,CancellationToken.None);
            else if(File.Exists(Path.Combine(input,"default.xex")))game.ImportFolder(input,report,CancellationToken.None);
            else {
                Progress(0.02,"Looking for the package");
                var found=Outrun.Setup.PortableGame.FindPackages(input,true,CancellationToken.None);
                if(found.Count==0){Console.Error.WriteLine("No OutRun Online Arcade package (version 0.0.1.5) was found in this folder.");return 3;}
                game.Import(found[0],report,CancellationToken.None);
            }
            if(!game.Ready()){Console.Error.WriteLine("The imported game files could not be verified.");return 4;}
            if(last<100)Progress(1,"Ready to play");return 0;
        }
        internal static int Play(Outrun.Setup.PortableGame game) {
            game.Recover();
            if(!game.Ready()){Console.Error.WriteLine("The game files are not imported yet.");return NotImported;}
            if(!File.Exists(Path.Combine(game.Resources,"outrun.exe"))){Console.Error.WriteLine("resources\\outrun.exe is missing.");return RuntimeMissing;}
            int code=0;var seen=new System.Collections.Generic.HashSet<int>();
            Process current=game.Launch();
            while(current!=null) {
                using(current) {
                    if(!seen.Add(current.Id))break;
                    try{var handle=current.Handle;}catch(InvalidOperationException){}catch(System.ComponentModel.Win32Exception){}
                    current.WaitForExit();
                    try{code=current.ExitCode;}catch(InvalidOperationException){}catch(System.ComponentModel.Win32Exception){}
                }
                current=game.RunningGame();
            }
            return code;
        }
    }
}
