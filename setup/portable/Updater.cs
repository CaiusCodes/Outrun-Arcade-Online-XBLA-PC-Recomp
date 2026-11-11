using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;

// The update window: "OutRun Online Arcade.exe --update" (docs/UPDATES.md).
//
// Opened by the game's Help & Options > Check for Updates once the player
// chose Update (the game closes itself). It waits for the game to exit, asks
// GitHub for the latest release (the same lookup as src/outrun_update.h),
// downloads its ZIP into resources\.update, refuses it unless its SHA-256
// matches the digest GitHub reports for the asset, unpacks it (unsafe names
// refused), checks that it is an OutRun release of that version, and replaces
// the program files only: OutRun Online Arcade.exe, README.txt, licenses\ and
// the program files in resources\. Player data is never written or deleted:
// resources\assets, userdata, cache and logs, outrun.toml, pc-settings.toml,
// multiplayer-settings.toml, import-manifest.json, userdata-location.txt and
// the outrun.toml backups (a chosen save folder outside resources is never seen).
// Then it starts the game again.
//
// The replacement can always be undone: every new file is first written beside
// its target as <name>.update-new; a journal (resources\.update-journal.txt)
// lists the targets; then each target becomes <name>.update-old and the new
// file takes its name. Any error puts every old file back. If the process is
// cut off half-way, the next start of the launcher finishes the job from the
// journal (RecoverInterrupted): back to the old files unless the journal says
// every file was swapped. Adapted from the Perfect Dark XBLA PC port's updater.
namespace Outrun.Setup {
    // Keep equal to CMake's OUTRUN_UPDATE_REPO / OUTRUN_UPDATE_ZIP_PREFIX (the
    // game's Check for Updates). An empty repository leaves only the
    // OUTRUN_UPDATE_API test override.
    internal static class UpdateSource {
        internal const string Repository="CaiusCodes/Outrun-Arcade-Online-XBLA-PC-Recomp";
        internal const string ZipPrefix="Outrun-Arcade-Online-XBLA-PC-Recomp-v";
        // OUTRUN_UPDATE_API (environment, for tests) points at another releases
        // URL: https, or http on 127.0.0.1 / localhost.
        internal static string ApiUrl() {
            string over=Environment.GetEnvironmentVariable("OUTRUN_UPDATE_API");
            if(!string.IsNullOrEmpty(over))return over;
            return Repository.Length==0?null:"https://api.github.com/repos/"+Repository+"/releases/latest";
        }
    }

    internal sealed class UpdateRelease {
        internal string Version,ZipName,ZipUrl,Sha256;
        internal long ZipSize;
    }

    internal sealed class UpdateException:Exception {internal UpdateException(string message):base(message){}}

    internal static class Updates {
        internal const string NewSuffix=".update-new",OldSuffix=".update-old";
        internal const string LauncherName="OutRun Online Arcade.exe",ReleaseFolder="OutRun Online Arcade";
        static readonly string[] ProtectedFolders={"assets","userdata","cache","logs",".update"};
        static readonly string[] ProtectedFiles={"outrun.toml","pc-settings.toml","multiplayer-settings.toml","import-manifest.json","userdata-location.txt",".import-journal.json",".import.lock",".update-journal.txt"};
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool MoveFileEx(string a,string b,uint flags);

        // ------------------------------------------------------------ log --
        internal static void Log(string root,string text) {
            try {
                string logs=Path.Combine(root,"resources","logs");Directory.CreateDirectory(logs);
                File.AppendAllText(Path.Combine(logs,"update.log"),DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss")+"  "+text+Environment.NewLine,new UTF8Encoding(false));
            }catch(IOException){}catch(UnauthorizedAccessException){}
        }

        // ------------------------------------------------------- versions --
        // "0.9.1", "v0.9.1", "0.9.0a", "0.9.0a-test5": the numbers, then a letter
        // (none < a < b), then a pre-release part after '-' (any pre-release is
        // older than the same version without one). As src/outrun_update.h.
        internal static int CompareVersions(string a,string b) {
            Func<string,Tuple<List<int>,string,string>> split=v=>{
                v=v??"";if(v.StartsWith("v")||v.StartsWith("V"))v=v.Substring(1);
                string pre="";int dash=v.IndexOf('-');if(dash>=0){pre=v.Substring(dash+1);v=v.Substring(0,dash);}
                var nums=new List<int>();string letter="";int i=0;
                while(i<v.Length){
                    int n=0;bool any=false;
                    while(i<v.Length&&v[i]>='0'&&v[i]<='9'){n=n*10+(v[i++]-'0');any=true;}
                    if(any)nums.Add(n);
                    if(i<v.Length&&v[i]=='.'){i++;continue;}
                    if(i<v.Length){letter=v.Substring(i);break;}
                }
                return Tuple.Create(nums,letter,pre);
            };
            var x=split(a);var y=split(b);
            for(int i=0;i<Math.Max(x.Item1.Count,y.Item1.Count);i++){
                int p=i<x.Item1.Count?x.Item1[i]:0,q=i<y.Item1.Count?y.Item1[i]:0;
                if(p!=q)return p<q?-1:1;
            }
            int c=string.CompareOrdinal(x.Item2,y.Item2);if(c!=0)return c<0?-1:1;
            if((x.Item3.Length==0)!=(y.Item3.Length==0))return x.Item3.Length==0?1:-1;
            c=string.CompareOrdinal(x.Item3,y.Item3);return c==0?0:c<0?-1:1;
        }

        // The installed game's version: resources\outrun.exe's product version
        // (CMake's OUTRUN_VERSION, the same the title screen shows).
        internal static string InstalledVersion(string resources) {
            string exe=Path.Combine(resources,"outrun.exe");
            if(!File.Exists(exe))return null;
            string v=FileVersionInfo.GetVersionInfo(exe).ProductVersion;
            return string.IsNullOrWhiteSpace(v)?null:v.Trim();
        }

        // ----------------------------------------------------------- HTTP --
        static void CheckAddress(Uri uri) {
            if(uri.Scheme==Uri.UriSchemeHttps)return;
            if(uri.Scheme==Uri.UriSchemeHttp&&(uri.Host=="127.0.0.1"||uri.Host=="localhost"))return;
            throw new UpdateException("only HTTPS addresses are allowed");
        }
        static HttpWebResponse Get(string url,string agent) {
            Uri uri;if(!Uri.TryCreate(url,UriKind.Absolute,out uri))throw new UpdateException("bad address");
            CheckAddress(uri);
            try{ServicePointManager.SecurityProtocol=SecurityProtocolType.Tls12|(SecurityProtocolType)12288;}
            catch(NotSupportedException){ServicePointManager.SecurityProtocol=SecurityProtocolType.Tls12;}
            var request=(HttpWebRequest)WebRequest.Create(uri);
            request.UserAgent=agent;request.Accept="application/vnd.github+json, application/octet-stream";
            request.Timeout=20000;request.ReadWriteTimeout=30000;request.AllowAutoRedirect=true;request.MaximumAutomaticRedirections=5;
            HttpWebResponse response;
            try{response=(HttpWebResponse)request.GetResponse();}
            catch(WebException ex){
                var r=ex.Response as HttpWebResponse;
                if(r==null)throw new UpdateException("could not reach GitHub (no internet connection?)");
                int status=(int)r.StatusCode;r.Close();
                throw new UpdateException(status==404?"no release was found":status==403||status==429?"GitHub is busy (too many checks); try again in an hour":"the server answered "+status);
            }
            try{CheckAddress(response.ResponseUri);}catch{response.Close();throw;}
            return response;
        }
        internal static string GetText(string url,string agent) {
            using(var response=Get(url,agent))using(var stream=response.GetResponseStream())using(var memory=new MemoryStream()){
                var buffer=new byte[65536];int n;
                while((n=stream.Read(buffer,0,buffer.Length))>0){memory.Write(buffer,0,n);if(memory.Length>(8<<20))throw new UpdateException("the answer was too large");}
                return Encoding.UTF8.GetString(memory.ToArray());
            }
        }
        internal static void Download(string url,string agent,string file,Action<long,long> progress,CancellationToken cancel) {
            using(var response=Get(url,agent))using(var stream=response.GetResponseStream())using(var output=new FileStream(file,FileMode.Create,FileAccess.Write,FileShare.None)){
                long total=response.ContentLength>0?response.ContentLength:0,done=0;var buffer=new byte[1<<16];int n;
                try {
                    while((n=stream.Read(buffer,0,buffer.Length))>0){
                        cancel.ThrowIfCancellationRequested();
                        output.Write(buffer,0,n);done+=n;if(progress!=null)progress(done,total);
                    }
                }catch(IOException ex){if(ex.InnerException is WebException||ex.InnerException is System.Net.Sockets.SocketException)throw new UpdateException("the download was interrupted");throw new UpdateException("could not write the download (disk full?)");}
                catch(WebException){throw new UpdateException("the download was interrupted");}
                if(total>0&&done!=total)throw new UpdateException("the download was incomplete");
            }
        }

        // -------------------------------------------------------- release --
        static bool IsSha256(string s){return s!=null&&s.Length==64&&s.All(c=>(c>='0'&&c<='9')||(c>='a'&&c<='f'));}
        static string Str(Dictionary<string,object> d,string key){object v;return d!=null&&d.TryGetValue(key,out v)?v as string:null;}

        // The latest release: its version and its ZIP with the ZIP's SHA-256.
        internal static UpdateRelease LatestRelease(string agent) {
            string api=UpdateSource.ApiUrl();if(api==null)throw new UpdateException("this copy has no update address");
            Dictionary<string,object> root;
            try{root=new JavaScriptSerializer{MaxJsonLength=8<<20}.DeserializeObject(GetText(api,agent)) as Dictionary<string,object>;}
            catch(ArgumentException){root=null;}catch(InvalidOperationException){root=null;}
            if(root==null)throw new UpdateException("GitHub's answer could not be read");
            var release=new UpdateRelease{Version=Str(root,"tag_name")??""};
            if(release.Version.StartsWith("v")||release.Version.StartsWith("V"))release.Version=release.Version.Substring(1);
            if(release.Version.Length==0)throw new UpdateException("the latest release has no version");
            string want=UpdateSource.ZipPrefix+release.Version+".zip",shaUrl=null;
            object assets;
            if(root.TryGetValue("assets",out assets)&&assets is IEnumerable)foreach(var item in (IEnumerable)assets){
                var a=item as Dictionary<string,object>;string name=Str(a,"name");
                if(name==want){
                    release.ZipName=name;release.ZipUrl=Str(a,"browser_download_url");
                    object size;if(a.TryGetValue("size",out size)&&size!=null)try{release.ZipSize=Convert.ToInt64(size);}catch(FormatException){}catch(InvalidCastException){}
                    string digest=(Str(a,"digest")??"").ToLowerInvariant();if(digest.StartsWith("sha256:"))release.Sha256=digest.Substring(7);
                }else if(name==want+".sha256")shaUrl=Str(a,"browser_download_url");
            }
            if(string.IsNullOrEmpty(release.ZipUrl))throw new UpdateException("the latest release (v"+release.Version+") has no "+want);
            if(!IsSha256(release.Sha256)&&shaUrl!=null){string text=GetText(shaUrl,agent).Trim();if(text.Length>=64)release.Sha256=text.Substring(0,64).ToLowerInvariant();}
            if(!IsSha256(release.Sha256))throw new UpdateException("the release's checksum is missing, so the download could not be checked");
            return release;
        }

        internal static string FileSha256(string path) {
            using(var sha=SHA256.Create())using(var input=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read,1<<20,FileOptions.SequentialScan))
                return BitConverter.ToString(sha.ComputeHash(input)).Replace("-","").ToLowerInvariant();
        }

        // ------------------------------------------------------------ ZIP --
        // Unpacks under `dest`. Absolute names, drive letters, ".." and names a
        // Windows folder can't hold safely are refused, as is anything over 4 GB.
        internal static void Extract(string zip,string dest,CancellationToken cancel) {
            Directory.CreateDirectory(dest);long total=0;
            ZipArchive archive;
            try{archive=ZipFile.OpenRead(zip);}catch(InvalidDataException){throw new UpdateException("the download is not a ZIP");}
            using(archive)foreach(var entry in archive.Entries){
                cancel.ThrowIfCancellationRequested();
                string name=entry.FullName.Replace('\\','/');
                if(name.Length==0||name.StartsWith("/")||name.Contains(":"))throw new UpdateException("the ZIP holds an unsafe name");
                var parts=name.TrimEnd('/').Split('/');
                try{foreach(string part in parts)Paths.CheckName(part);}catch(InvalidDataException){throw new UpdateException("the ZIP holds an unsafe name");}
                string target;try{target=Paths.Child(dest,string.Join("\\",parts));}catch(InvalidDataException){throw new UpdateException("the ZIP holds an unsafe name");}
                if(name.EndsWith("/")){Directory.CreateDirectory(target);continue;}
                total+=entry.Length;if(total>(4L<<30))throw new UpdateException("the ZIP is too large");
                Directory.CreateDirectory(Path.GetDirectoryName(target));
                try{entry.ExtractToFile(target,false);}catch(InvalidDataException){throw new UpdateException("the ZIP is damaged");}
            }
        }

        // ---------------------------------------------------- what changes --
        static bool Is(string a,string b){return string.Equals(a,b,StringComparison.OrdinalIgnoreCase);}
        // Player data and the launcher's own work files, never written or deleted.
        internal static bool IsProtected(string relative) {
            var parts=relative.Replace('/','\\').Split('\\');
            if(parts.Length<2||!Is(parts[0],"resources"))return false;
            if(parts.Length>2)return ProtectedFolders.Any(f=>Is(parts[1],f))||parts[1].StartsWith(".import",StringComparison.OrdinalIgnoreCase);
            string file=parts[1];
            return ProtectedFiles.Any(f=>Is(file,f))||file.StartsWith(".import",StringComparison.OrdinalIgnoreCase)||
                   (file.StartsWith("outrun.toml.",StringComparison.OrdinalIgnoreCase)&&file.EndsWith(".bak",StringComparison.OrdinalIgnoreCase))||
                   file.EndsWith(NewSuffix,StringComparison.OrdinalIgnoreCase)||file.EndsWith(OldSuffix,StringComparison.OrdinalIgnoreCase);
        }
        // The program files of an unpacked release (relative paths): the files at
        // its top, licenses\ and resources\ less player data. Anything else is
        // left out and logged.
        internal static List<string> ProgramFiles(string release,List<string> skipped) {
            var files=new List<string>();
            foreach(string full in Directory.GetFiles(release,"*",SearchOption.AllDirectories).OrderBy(p=>p,StringComparer.OrdinalIgnoreCase)){
                string rel=full.Substring(release.Length+1);var parts=rel.Split('\\');
                bool keep=parts.Length==1||Is(parts[0],"licenses")||(Is(parts[0],"resources")&&!IsProtected(rel));
                if(keep&&(rel.EndsWith(NewSuffix,StringComparison.OrdinalIgnoreCase)||rel.EndsWith(OldSuffix,StringComparison.OrdinalIgnoreCase)))keep=false;
                if(keep)files.Add(rel);else skipped.Add(rel);
            }
            return files;
        }

        // ---------------------------------------------------------- install --
        static string JournalPath(string root){return Path.Combine(root,"resources",".update-journal.txt");}
        static void Swap(string from,string to){if(!MoveFileEx(from,to,1/*REPLACE_EXISTING*/))throw new IOException("move failed",Marshal.GetHRForLastWin32Error());}

        internal static void Install(string root,string release,List<string> files,Action<string> log) {
            if(files.Count==0)throw new UpdateException("the release is empty");
            // 1. Every new file beside its target.
            var staged=new List<string>();
            Action unstage=()=>{foreach(string f in staged)try{File.Delete(f);}catch(IOException){}catch(UnauthorizedAccessException){}};
            foreach(string rel in files){
                string target=Paths.Child(root,rel),fresh=target+NewSuffix;
                try{Directory.CreateDirectory(Path.GetDirectoryName(target));File.Copy(Path.Combine(release,rel),fresh,true);}
                catch(Exception ex){
                    if(!(ex is IOException||ex is UnauthorizedAccessException))throw;
                    unstage();throw new UpdateException("could not write "+rel+" (is the folder read-only or the disk full?). Nothing was changed");
                }
                staged.Add(fresh);
            }
            // 2. The journal, then each swap; undo all of them on any failure.
            var had=files.ToDictionary(rel=>rel,rel=>File.Exists(Paths.Child(root,rel)),StringComparer.OrdinalIgnoreCase);
            File.WriteAllLines(JournalPath(root),files.Select(rel=>(had[rel]?"1\t":"0\t")+rel),new UTF8Encoding(false));
            var done=new List<string>();
            int failAfter=-1;int.TryParse(Environment.GetEnvironmentVariable("OUTRUN_UPDATE_TEST_FAIL_AFTER")??"-1",out failAfter);
            foreach(string rel in files){
                string target=Paths.Child(root,rel);
                try{
                    if(failAfter>=0&&done.Count==failAfter)throw new IOException("test failure (OUTRUN_UPDATE_TEST_FAIL_AFTER)");
                    if(had[rel])Swap(target,target+OldSuffix);
                    try{Swap(target+NewSuffix,target);}
                    catch{if(had[rel])Swap(target+OldSuffix,target);throw;}
                    done.Add(rel);
                }catch(Exception ex){
                    if(!(ex is IOException||ex is UnauthorizedAccessException))throw;
                    log("Update: could not replace "+rel+" ("+ex.Message+"); putting back the "+done.Count+" files already replaced");
                    Rollback(root,done,had);
                    unstage();
                    try{File.Delete(JournalPath(root));}catch(IOException){}
                    throw new UpdateException("could not replace "+rel+" (is a file in use?). Every file was put back; nothing was changed");
                }
            }
            File.AppendAllText(JournalPath(root),"committed"+Environment.NewLine);
            // 3. The old files go (this launcher's own exe stays until the next start).
            foreach(string rel in done)if(had[rel])try{File.Delete(Paths.Child(root,rel)+OldSuffix);}catch(IOException){}catch(UnauthorizedAccessException){}
            try{File.Delete(JournalPath(root));}catch(IOException){}
        }

        static void Rollback(string root,IEnumerable<string> swapped,Dictionary<string,bool> had) {
            foreach(string rel in swapped.Reverse()){
                string target=Paths.Child(root,rel),old=target+OldSuffix;
                try{
                    if(File.Exists(old)){Swap(old,target);}
                    else if(!had[rel]&&File.Exists(target))File.Delete(target);
                }catch(IOException){}catch(UnauthorizedAccessException){}
            }
        }

        // At every launcher start: an update cut off half-way is undone (or, if
        // every file was already swapped, finished), and leftovers are cleared.
        // Skipped while an update window for this folder is running.
        internal static void RecoverInterrupted(string root) {
            using(var mutex=new Mutex(false,MutexName(root))){
                bool owned;try{owned=mutex.WaitOne(0);}catch(AbandonedMutexException){owned=true;}
                if(!owned)return;
                try{RecoverLocked(root);}finally{mutex.ReleaseMutex();}
            }
        }
        static void RecoverLocked(string root) {
            string journal=JournalPath(root),work=Path.Combine(root,"resources",".update");
            if(File.Exists(journal)){
                var lines=File.ReadAllLines(journal);bool committed=lines.Any(l=>l.Trim()=="committed");
                var entries=lines.Where(l=>l.Length>2&&l[1]=='\t').Select(l=>Tuple.Create(l[0]=='1',l.Substring(2))).ToList();
                foreach(var e in entries){
                    string target;try{target=Paths.Child(root,e.Item2);}catch(InvalidDataException){continue;}
                    if(IsProtected(e.Item2))continue;
                    string old=target+OldSuffix;
                    try{
                        if(committed){if(File.Exists(old))File.Delete(old);}
                        else if(File.Exists(old)){Swap(old,target);Log(root,"Update: put back "+e.Item2+" (an update was cut off).");}
                        else if(!e.Item1&&File.Exists(target)){File.Delete(target);Log(root,"Update: removed "+e.Item2+" (an update was cut off).");}
                    }catch(IOException){}catch(UnauthorizedAccessException){}
                }
                try{File.Delete(journal);}catch(IOException){}
            }
            // Leftover new/old copies in the program folders (never in player data).
            var folders=new List<Tuple<string,SearchOption>>{Tuple.Create(root,SearchOption.TopDirectoryOnly),Tuple.Create(Path.Combine(root,"licenses"),SearchOption.AllDirectories),Tuple.Create(Path.Combine(root,"resources"),SearchOption.TopDirectoryOnly)};
            string resources=Path.Combine(root,"resources");
            if(Directory.Exists(resources))foreach(string dir in Directory.GetDirectories(resources)){
                string name=Path.GetFileName(dir);
                if(!ProtectedFolders.Any(f=>Is(name,f))&&!name.StartsWith(".import",StringComparison.OrdinalIgnoreCase))folders.Add(Tuple.Create(dir,SearchOption.AllDirectories));
            }
            foreach(var f in folders){
                if(!Directory.Exists(f.Item1))continue;
                foreach(string file in Directory.GetFiles(f.Item1,"*.update-*",f.Item2)){
                    try{
                        if(file.EndsWith(NewSuffix,StringComparison.OrdinalIgnoreCase))File.Delete(file);
                        else if(file.EndsWith(OldSuffix,StringComparison.OrdinalIgnoreCase)){
                            string target=file.Substring(0,file.Length-OldSuffix.Length);
                            if(!File.Exists(target)){Swap(file,target);Log(root,"Update: put back "+Path.GetFileName(target)+" (an update was cut off).");}
                            else File.Delete(file);
                        }
                    }catch(IOException){}catch(UnauthorizedAccessException){}
                }
            }
            if(Directory.Exists(work))try{Directory.Delete(work,true);}catch(IOException){}catch(UnauthorizedAccessException){}
        }

        internal static string MutexName(string root) {
            using(var sha=SHA256.Create())
                return "Local\\OutRunOnlineArcadeUpdate-"+BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(Path.GetFullPath(root).TrimEnd('\\').ToUpperInvariant())),0,8).Replace("-","");
        }

        internal static bool GameRunning(string resources) {
            foreach(var p in Process.GetProcessesByName("outrun"))using(p){
                try{if(Is(Path.GetDirectoryName(p.MainModule.FileName),resources))return true;}
                catch(System.ComponentModel.Win32Exception){return true;}catch(InvalidOperationException){}
            }
            return false;
        }
    }

    // The window: the launcher's coastal header, one status card, Cancel/Close
    // and Play.
    internal sealed class UpdateForm:Form {
        enum Stage{Waiting,Checking,Downloading,Verifying,Unpacking,Installing,Updated,UpToDate,Failed,Cancelled}
        readonly PortableGame game;
        readonly Label title=new Label(),phase=new Label(),detail=new Label(),stats=new Label(),copy=new Label();
        readonly ImportProgress progress=new ImportProgress();
        readonly ArcadeButton left=new ArcadeButton(),play=new ArcadeButton();
        readonly CancellationTokenSource cancel=new CancellationTokenSource();
        readonly System.Windows.Forms.Timer autoStart=new System.Windows.Forms.Timer{Interval=1800};
        Stage stage=Stage.Waiting;string version="",error="",installed;long done,total;
        internal bool Succeeded{get{return stage==Stage.Updated||stage==Stage.UpToDate;}}

        internal UpdateForm(PortableGame game) {
            this.game=game;installed=Updates.InstalledVersion(game.Resources)??"unknown";
            SuspendLayout();Text="OutRun Online Arcade Update";AutoScaleDimensions=new SizeF(96,96);AutoScaleMode=AutoScaleMode.Dpi;
            Font=new Font("Segoe UI",10);BackColor=CoastPalette.Paper;ForeColor=CoastPalette.Ink;
            ClientSize=new Size(640,420);FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;StartPosition=FormStartPosition.CenterScreen;
            try{Icon=Icon.ExtractAssociatedIcon(Application.ExecutablePath);}catch(ArgumentException){}
            Controls.Add(new CoastScene{Location=Point.Empty,Size=new Size(640,157)});
            title.Location=new Point(28,174);title.Size=new Size(584,32);title.Font=new Font("Segoe UI",15,FontStyle.Bold);Controls.Add(title);
            phase.Location=new Point(28,214);phase.Size=new Size(400,22);phase.Font=new Font("Segoe UI",9,FontStyle.Bold);Controls.Add(phase);
            stats.Location=new Point(432,214);stats.Size=new Size(180,22);stats.Font=new Font("Segoe UI",9);stats.ForeColor=CoastPalette.Muted;stats.TextAlign=ContentAlignment.TopRight;Controls.Add(stats);
            progress.Location=new Point(28,240);progress.Size=new Size(584,6);Controls.Add(progress);
            detail.Location=new Point(28,256);detail.Size=new Size(584,58);detail.Font=new Font("Segoe UI",9);detail.ForeColor=CoastPalette.Muted;Controls.Add(detail);
            var footer=new Panel{Location=new Point(0,330),Size=new Size(640,90),BackColor=CoastPalette.Ink};Controls.Add(footer);
            copy.Text="This copy: version "+installed;copy.Location=new Point(28,34);copy.Size=new Size(260,24);copy.Font=new Font("Segoe UI",9);copy.ForeColor=Color.FromArgb(192,219,219);footer.Controls.Add(copy);
            left.Text="Cancel";left.Bounds=new Rectangle(300,22,126,46);left.Font=new Font("Segoe UI",10,FontStyle.Bold);footer.Controls.Add(left);
            play.Text="Play";play.Bounds=new Rectangle(446,18,166,54);play.Primary=true;play.Font=new Font("Segoe UI",16,FontStyle.Bold|FontStyle.Italic);footer.Controls.Add(play);
            left.Click+=(s,e)=>{if(stage<=Stage.Downloading){cancel.Cancel();left.Enabled=false;Show(stage);}else Close();};
            play.Click+=(s,e)=>StartGame();
            autoStart.Tick+=(s,e)=>{autoStart.Stop();StartGame();};
            FormClosing+=(s,e)=>{if(stage>=Stage.Verifying&&stage<=Stage.Installing){e.Cancel=true;return;}if(stage<=Stage.Downloading)cancel.Cancel();};
            Shown+=async(s,e)=>{Show(Stage.Waiting);await Task.Run(()=>Work());};
            ResumeLayout(false);PerformLayout();
        }
        protected override void Dispose(bool disposing){if(disposing){autoStart.Dispose();cancel.Dispose();}base.Dispose(disposing);}
        // Background tests (OUTRUN_UPDATE_BACKGROUND=1): opens in the top-right
        // corner without taking the focus.
        static readonly bool background=Environment.GetEnvironmentVariable("OUTRUN_UPDATE_BACKGROUND")=="1";
        protected override bool ShowWithoutActivation{get{return background;}}
        protected override CreateParams CreateParams{get{var p=base.CreateParams;if(background)p.ExStyle|=0x08000000/*WS_EX_NOACTIVATE*/;return p;}}
        protected override void OnLoad(EventArgs e){
            base.OnLoad(e);
            if(background){var area=Screen.PrimaryScreen.WorkingArea;StartPosition=FormStartPosition.Manual;Location=new Point(area.Right-Width-8,area.Top+380);}
        }

        void StartGame() {
            try{game.Launch();Updates.Log(game.Root,"Update window: game started.");Close();}
            catch(Exception ex){autoStart.Stop();error="The game could not start: "+ex.Message;Show(Stage.Failed);}
        }

        void Set(Stage s){if(IsDisposed)return;try{BeginInvoke((Action)(()=>Show(s)));}catch(InvalidOperationException){}}
        void Show(Stage s) {
            stage=s;string v=version.Length>0?"v"+version:"";
            bool cancelling=cancel.IsCancellationRequested&&s<=Stage.Downloading;
            progress.Indeterminate=s==Stage.Waiting||s==Stage.Checking;progress.Visible=s<=Stage.Installing;
            stats.Text="";phase.ForeColor=CoastPalette.Teal;detail.ForeColor=CoastPalette.Muted;
            detail.Text="Only the program files change. Your saves, settings and game files are kept.";
            switch(s){
                case Stage.Waiting:title.Text="Updating OutRun";phase.Text="WAITING FOR THE GAME TO CLOSE";break;
                case Stage.Checking:title.Text="Updating OutRun";phase.Text="CHECKING GITHUB FOR THE LATEST RELEASE";break;
                case Stage.Downloading:
                    title.Text="Updating to "+v;phase.Text="DOWNLOADING";
                    stats.Text=(done/1048576.0).ToString("0.0")+(total>0?" / "+(total/1048576.0).ToString("0.0"):"")+" MB";
                    progress.Value=total>0?(int)(done*100/total):0;break;
                case Stage.Verifying:title.Text="Updating to "+v;phase.Text="CHECKING THE DOWNLOAD";progress.Value=100;break;
                case Stage.Unpacking:title.Text="Updating to "+v;phase.Text="UNPACKING";progress.Value=100;break;
                case Stage.Installing:title.Text="Updating to "+v;phase.Text="INSTALLING";progress.Value=100;break;
                case Stage.Updated:copy.Text="This copy: version "+(Updates.InstalledVersion(game.Resources)??version);title.Text="Updated to "+v;phase.Text="STARTING OUTRUN";phase.ForeColor=CoastPalette.Teal;detail.Text="The new version starts in a moment. Your saves and settings were kept.";break;
                case Stage.UpToDate:title.Text="OutRun is up to date";phase.Text="NO NEWER VERSION";detail.Text="You have the latest version (v"+installed+").";break;
                case Stage.Failed:title.Text="The update did not complete";phase.Text="UPDATE FAILED";phase.ForeColor=CoastPalette.Red;detail.ForeColor=CoastPalette.Ink;detail.Text=error+" Details: resources\\logs\\update.log";break;
                case Stage.Cancelled:title.Text="Update cancelled";phase.Text="NOTHING WAS CHANGED";phase.ForeColor=CoastPalette.Muted;break;
            }
            if(cancelling)phase.Text="CANCELLING";
            bool busy=s>=Stage.Verifying&&s<=Stage.Installing;
            left.Text=s<=Stage.Downloading?"Cancel":"Close";left.Enabled=!busy&&!cancelling;
            play.Enabled=s>=Stage.Updated&&s!=Stage.Updated;
            if(s==Stage.Updated){play.Enabled=false;left.Enabled=false;autoStart.Start();}
            if((s==Stage.Failed||s==Stage.Updated)&&!ContainsFocus&&!background)FlashWindow();
        }
        [DllImport("user32.dll")]static extern bool FlashWindowEx(ref FLASHWINFO info);
        [StructLayout(LayoutKind.Sequential)]struct FLASHWINFO{public uint cbSize;public IntPtr hwnd;public uint dwFlags,uCount,dwTimeout;}
        void FlashWindow(){var f=new FLASHWINFO{cbSize=(uint)Marshal.SizeOf(typeof(FLASHWINFO)),hwnd=Handle,dwFlags=3|12};FlashWindowEx(ref f);}

        void Work() {
            string root=game.Root,work=Path.Combine(game.Resources,".update");
            Action<string> fail=why=>{Updates.Log(root,"Update FAILED: "+why);error=why;Set(cancel.IsCancellationRequested?Stage.Cancelled:Stage.Failed);};
            Action clean=()=>{try{if(Directory.Exists(work))Directory.Delete(work,true);}catch(IOException){}catch(UnauthorizedAccessException){}};
            try {
                // The game closes itself before opening this; give it a moment.
                for(int i=0;i<150&&Updates.GameRunning(game.Resources);i++){if(cancel.IsCancellationRequested){Set(Stage.Cancelled);return;}Thread.Sleep(200);}
                if(Updates.GameRunning(game.Resources)){fail("OutRun is still running from this folder. Close it, then try again.");return;}
                Set(Stage.Checking);
                string agent="OutRun-Online-Arcade-PC-Launcher/"+installed;
                UpdateRelease release;
                try{release=Updates.LatestRelease(agent);}catch(UpdateException ex){fail("Could not check for updates: "+ex.Message+".");return;}
                version=release.Version;total=release.ZipSize;
                Updates.Log(root,"Update: latest release v"+release.Version+", this copy v"+installed);
                if(installed!="unknown"&&Updates.CompareVersions(release.Version,installed)<=0){Set(Stage.UpToDate);return;}
                if(cancel.IsCancellationRequested){Set(Stage.Cancelled);return;}
                clean();Directory.CreateDirectory(work);
                string zip=Path.Combine(work,release.ZipName);
                Set(Stage.Downloading);long last=0;
                try{
                    Updates.Download(release.ZipUrl,agent,zip,(d,t)=>{done=d;if(t>0)total=t;long now=Environment.TickCount;if(now-last>100){last=now;Set(Stage.Downloading);}},cancel.Token);
                }catch(OperationCanceledException){clean();Set(Stage.Cancelled);return;}
                catch(UpdateException ex){clean();fail("The download did not finish: "+ex.Message+". Nothing was changed.");return;}
                Set(Stage.Verifying);
                string sha=Updates.FileSha256(zip);
                if(sha!=release.Sha256){clean();Updates.Log(root,"Update: SHA-256 "+sha+", expected "+release.Sha256);fail("The download did not match the release's checksum, so it was not used. Nothing was changed.");return;}
                Updates.Log(root,"Update: downloaded "+release.ZipName+", SHA-256 verified.");
                if(cancel.IsCancellationRequested){clean();Set(Stage.Cancelled);return;}
                Set(Stage.Unpacking);
                string unpacked=Path.Combine(work,"new");
                try{Updates.Extract(zip,unpacked,CancellationToken.None);}
                catch(UpdateException ex){clean();fail("The download could not be unpacked: "+ex.Message+". Nothing was changed.");return;}
                string releaseRoot=Path.Combine(unpacked,Updates.ReleaseFolder);
                string newVersion=File.Exists(Path.Combine(releaseRoot,Updates.LauncherName))?Updates.InstalledVersion(Path.Combine(releaseRoot,"resources")):null;
                if(newVersion==null){clean();fail("The download is not an OutRun Online Arcade release. Nothing was changed.");return;}
                if(Updates.CompareVersions(newVersion,release.Version)!=0){clean();fail("The download holds version "+newVersion+", not "+release.Version+". Nothing was changed.");return;}
                Set(Stage.Installing);
                var skipped=new List<string>();var files=Updates.ProgramFiles(releaseRoot,skipped);
                foreach(string rel in skipped)Updates.Log(root,"Update: left out "+rel+" (player data or not a program file).");
                try{
                    using(game.Lock()){
                        game.CheckNotRunning();
                        Updates.Install(root,releaseRoot,files,text=>Updates.Log(root,text));
                    }
                }catch(UpdateException ex){clean();fail("The update could not be installed: "+ex.Message+".");return;}
                catch(IOException ex){clean();fail("The update could not be installed: "+ex.Message+" Nothing was changed.");return;}
                clean();
                Updates.Log(root,"Update: installed v"+release.Version+" ("+files.Count+" program files).");
                Set(Stage.Updated);
            }catch(Exception ex){
                clean();fail("Unexpected error: "+ex.Message);
            }
        }
    }
}
