using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Threading;
using System.Windows.Forms;
namespace Outrun.Setup {
    internal static class PortableTests {
        static void Check(bool value,string message){if(!value)throw new Exception(message);}
        static string Hash(string p){using(var s=File.OpenRead(p))using(var h=SHA256.Create())return BitConverter.ToString(h.ComputeHash(s));}
        static PortableGame Prepare(string root,string stage) {
            Directory.CreateDirectory(root);foreach(string file in Directory.GetFiles(stage))File.Copy(file,Path.Combine(root,Path.GetFileName(file)));
            PortableGame.CopyTree(Path.Combine(stage,"resources"),Path.Combine(root,"resources"),CancellationToken.None);return new PortableGame(root);
        }
        static void ExpectFailure(Action action,string message){bool failed=false;try{action();}catch{failed=true;}Check(failed,message);}
        [STAThread]static int Main(string[] args) {
            string root=args[0],package=args[1],stage=args[2],reference=args[3];Directory.CreateDirectory(root);
            string originalHash=Hash(package);
            try {
                Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
                var fresh=Prepare(Path.Combine(root,"fresh"),stage);
                using(var form=new ImportForm(fresh,null,true)) {
                    form.RefreshReady();Check(!form.PlayEnabled&&form.BrowseEnabled,"Fresh Play gating wrong");
                    Check(form.Controls.Cast<Control>().SelectMany(c=>c.Controls.Cast<Control>()).OfType<CheckBox>().All(c=>!c.Checked),"Extras are checked by default");
                }
                ExpectFailure(()=>fresh.LaunchInfo(),"Play accepted before import");
                string invalid=Path.Combine(root,"invalid.txt");File.WriteAllText(invalid,"bad package");ExpectFailure(()=>fresh.Import(invalid,(p,s)=>{},CancellationToken.None),"Invalid input accepted");Check(!Directory.Exists(fresh.Assets),"Invalid package created assets");
                var cancelled=new CancellationTokenSource();cancelled.Cancel();ExpectFailure(()=>fresh.Import(package,(p,s)=>{},cancelled.Token),"Pre-cancel ignored");
                cancelled=new CancellationTokenSource();ExpectFailure(()=>fresh.Import(package,(p,s)=>{if(p>=10)cancelled.Cancel();},cancelled.Token),"Mid-import cancel ignored");
                Check(!fresh.Ready()&&!Directory.Exists(fresh.Assets)&&Directory.GetDirectories(fresh.Resources,".import-*").Length==0,"Cancellation left live assets/staging");
                Console.WriteLine("PASS: invalid input, Play gating, unchecked Extras and mid-import cancellation");
                fresh.Import(package,(p,s)=>{},CancellationToken.None);Check(fresh.Ready(),"Fresh import not ready");
                var originals=Directory.GetFiles(reference,"*",SearchOption.AllDirectories);foreach(string p in originals){string relative=p.Substring(reference.Length+1);Check(Hash(p)==Hash(Path.Combine(fresh.Assets,relative)),"Asset mismatch: "+relative);}
                Check(Directory.GetFiles(fresh.Assets,"*",SearchOption.AllDirectories).Length==originals.Length,"Asset inventory differs");
                using(var form=new ImportForm(fresh,null,true)){form.RefreshReady();Check(form.PlayEnabled&&!form.BrowseEnabled,"Post-import Play/Browse gating wrong");}
                ExpectFailure(()=>fresh.Import(invalid,(p,s)=>{},CancellationToken.None),"Working install reimported");Check(fresh.Ready(),"Rejected import damaged working install");
                var restart=new PortableGame(fresh.Root);var info=restart.LaunchInfo();Check(info.WorkingDirectory==fresh.Resources&&info.FileName==Path.Combine(fresh.Resources,"outrun.exe"),"Runtime launch paths wrong");
                Check(PortableGame.FindPackages(fresh.Root,false,CancellationToken.None).Count==0&&restart.Ready(),"Subsequent launch requires package");
                Console.WriteLine("PASS: fresh import, "+originals.Length+" exact asset hashes, Play ready, subsequent launch without package");
                Directory.CreateDirectory(Path.Combine(fresh.Resources,"userdata"));string save=Path.Combine(fresh.Resources,"userdata/save.dat");File.WriteAllBytes(save,new byte[]{1,3,5,7,9});
                File.WriteAllText(Path.Combine(fresh.Resources,"pc-settings.toml"),"# native\nfullscreen = false\nresolution_scale = 2\nshow_fps = true\n");
                File.WriteAllText(Path.Combine(fresh.Resources,"multiplayer-settings.toml"),"player_name = \"Keep Me\"\nshow_ping = true\n");
                string config=Path.Combine(fresh.Resources,"outrun.toml");File.AppendAllText(config,"\n# preserve custom setting\ncustom_setting = 123\n");
                string savedHash=Hash(save),nameHash=Hash(Path.Combine(fresh.Resources,"multiplayer-settings.toml"));
                string desktop=Path.Combine(root,"desktop");Directory.CreateDirectory(desktop);string link=Path.Combine(desktop,"OutRun Online Arcade.lnk");File.WriteAllText(link,"existing shortcut");string configHash=Hash(config);
                ExpectFailure(()=>fresh.ApplyExtras(true,true,desktop),"Existing shortcut overwritten");Check(File.ReadAllText(link)=="existing shortcut"&&Hash(config)==configHash,"Shortcut collision changed data");File.Delete(link);
                fresh.ApplyExtras(true,true,desktop);Check(File.ReadAllText(config).Contains("fullscreen = true")&&File.ReadAllText(config).Contains("custom_setting = 123"),"Fullscreen lost unrelated config");
                Check(File.ReadAllText(Path.Combine(fresh.Resources,"pc-settings.toml")).Contains("fullscreen = true")&&File.ReadAllText(Path.Combine(fresh.Resources,"pc-settings.toml")).Contains("resolution_scale = 2"),"Native fullscreen/preferences wrong");
                object shell=Activator.CreateInstance(Type.GetTypeFromProgID("WScript.Shell")),shortcut=null;try{shortcut=shell.GetType().InvokeMember("CreateShortcut",System.Reflection.BindingFlags.InvokeMethod,null,shell,new object[]{link});string target=(string)shortcut.GetType().InvokeMember("TargetPath",System.Reflection.BindingFlags.GetProperty,null,shortcut,null);Check(target==Path.Combine(fresh.Root,"OutRun Online Arcade.exe"),"Shortcut targets runtime");}finally{if(shortcut!=null)Marshal.FinalReleaseComObject(shortcut);Marshal.FinalReleaseComObject(shell);}
                Check(Hash(save)==savedHash&&Hash(Path.Combine(fresh.Resources,"multiplayer-settings.toml"))==nameHash,"Extras changed save/name");
                Console.WriteLine("PASS: Extras applied on request, native fullscreen, unrelated settings/saves, shortcut target/collision preservation");
                var migrate=Prepare(Path.Combine(root,"migrated"),stage);migrate.Migrate(fresh.Root,(p,s)=>{},CancellationToken.None);Check(migrate.Ready()&&fresh.Ready(),"Migration readiness/source lost");
                Check(Hash(Path.Combine(migrate.Resources,"userdata/save.dat"))==savedHash&&Hash(Path.Combine(migrate.Resources,"multiplayer-settings.toml"))==nameHash&&Hash(Path.Combine(migrate.Resources,"outrun.toml"))==Hash(config),"Migration changed saved settings/data");
                var rollback=Prepare(Path.Combine(root,"rollback"),stage);Directory.CreateDirectory(rollback.Assets);File.WriteAllText(Path.Combine(rollback.Assets,"keep.txt"),"original orphan assets");
                rollback.AfterMoveForTest=i=>{if(i==1)throw new IOException("Injected commit failure");};ExpectFailure(()=>rollback.Migrate(fresh.Root,(p,s)=>{},CancellationToken.None),"Commit failure not surfaced");
                Check(File.ReadAllText(Path.Combine(rollback.Assets,"keep.txt"))=="original orphan assets"&&!File.Exists(Path.Combine(rollback.Resources,"import-manifest.json")),"Rollback damaged prior data");
                rollback.AfterMoveForTest=null;rollback.Recover();Check(File.ReadAllText(Path.Combine(rollback.Assets,"keep.txt"))=="original orphan assets","Recovery damaged preserved data");
                string abandoned=Path.Combine(rollback.Resources,".import-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(abandoned);File.WriteAllText(Path.Combine(abandoned,"partial.txt"),"partial");rollback.Recover();Check(!Directory.Exists(abandoned),"Interrupted extraction not recovered");
                Console.WriteLine("PASS: copy migration, old installation preservation, commit rollback and interrupted-stage recovery");
                Check(Hash(package)==originalHash,"Original package modified");Console.WriteLine("PASS: original package unchanged; all portable launcher checks passed");return 0;
            }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
        }
    }
}
