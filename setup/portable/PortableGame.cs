using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Web.Script.Serialization;

namespace Outrun.Setup {
    internal sealed class ImportedFile { public string path; public long bytes; }
    internal sealed class ImportManifest {
        public int schema=1,assetPatchVersion=0;
        public string titleId="58410968",revision="0.0.1.5",runtimePatchVersion="20261013-native-mouse",xexSha256=GameValidation.XexSha256;
        public List<ImportedFile> files=new List<ImportedFile>();
    }
    internal sealed class ImportMove {public string source,target,backup;public bool existed;}
    internal sealed class ImportJournal {public string stage;public bool committed;public List<ImportMove> moves=new List<ImportMove>();}
    // Journal/staging approach adapted from the project-owned BBR launcher.
    // OutRun retains its own package validation, artwork and runtime patches.
    internal sealed class PortableGame {
        internal readonly string Root,Resources,Assets;
        readonly JavaScriptSerializer json=new JavaScriptSerializer();
#if OUTRUN_PORTABLE_TESTS
        internal Action<int> AfterMoveForTest;
#endif
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool MoveFileEx(string a,string b,uint flags);
        internal PortableGame(string root) {
            Root=Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar);
            if(Root.Length<4)throw new IOException("Extract into a portable folder below a drive root.");
            Resources=Path.Combine(Root,"resources");Assets=Path.Combine(Resources,"assets");Paths.CheckParents(Resources);
        }
        string ManifestPath {get{return Path.Combine(Resources,"import-manifest.json");}}
        string JournalPath {get{return Path.Combine(Resources,".import-journal.json");}}
        static bool Exists(string p){return File.Exists(p)||Directory.Exists(p);}
        static void Move(string a,string b){Directory.CreateDirectory(Path.GetDirectoryName(b));if(Directory.Exists(a))Directory.Move(a,b);else File.Move(a,b);}
        static void Remove(string p){Paths.CheckParents(p);if(Directory.Exists(p))Directory.Delete(p,true);else if(File.Exists(p))File.Delete(p);}
        internal void WriteJson(string path,object value) {
            string temp=path+".tmp-"+Guid.NewGuid().ToString("N");Paths.CheckParents(path);
            try {
                using(var file=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
                    byte[] bytes=Encoding.UTF8.GetBytes(json.Serialize(value));file.Write(bytes,0,bytes.Length);file.Flush(true);
                }
                if(!MoveFileEx(temp,path,1|8))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }finally{if(File.Exists(temp))File.Delete(temp);}
        }
        internal FileStream Lock(){Directory.CreateDirectory(Resources);Paths.CheckParents(Resources);return new FileStream(Path.Combine(Resources,".import.lock"),FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None,1,FileOptions.DeleteOnClose);}
        internal bool Ready() {
            try {
                if(File.Exists(JournalPath))return false;
                var m=json.Deserialize<ImportManifest>(File.ReadAllText(ManifestPath));
                if(m==null||m.schema!=1||m.titleId!="58410968"||m.revision!="0.0.1.5"||m.xexSha256!=GameValidation.XexSha256||m.assetPatchVersion!=0||m.files==null||m.files.Count==0)return false;
                if(!m.files.Any(f=>f.path=="default.xex"))return false;
                foreach(var f in m.files){string path=Paths.Child(Assets,f.path);if(!File.Exists(path)||new FileInfo(path).Length!=f.bytes)return false;}
                GameValidation.Check(Assets);return File.Exists(Path.Combine(Resources,"outrun.toml"));
            }catch(IOException){return false;}catch(ArgumentException){return false;}catch(InvalidOperationException){return false;}
        }
        internal static void ValidatePackage(string path) {
            using(var package=new StfsPackage(path,false))using(var sha=SHA256.Create())
                if(BitConverter.ToString(sha.ComputeHash(package.ReadFile("default.xex"))).Replace("-","")!=GameValidation.XexSha256)
                    throw new InvalidDataException("Unsupported OutRun executable revision. This runtime requires version 0.0.1.5.");
        }
        internal static List<string> FindPackages(string folder,bool nested,CancellationToken cancel) {
            var result=new List<string>();var queue=new Queue<Tuple<string,int>>();queue.Enqueue(Tuple.Create(Path.GetFullPath(folder),0));int files=0,folders=0;
            while(queue.Count>0) {
                cancel.ThrowIfCancellationRequested();var current=queue.Dequeue();Paths.CheckParents(current.Item1);
                foreach(string path in Directory.EnumerateFiles(current.Item1)) {
                    cancel.ThrowIfCancellationRequested();if(++files>2048)throw new IOException("Choose a smaller package folder.");
                    try {
                        using(var input=File.OpenRead(path)){byte[] magic=new byte[4];if(input.Read(magic,0,4)!=4)continue;string tag=Encoding.ASCII.GetString(magic);if(tag!="LIVE"&&tag!="PIRS"&&tag!="CON ")continue;}
                        ValidatePackage(path);result.Add(path);
                    }catch(IOException){}catch(UnauthorizedAccessException){}
                }
                if(nested&&current.Item2<4)foreach(string child in Directory.EnumerateDirectories(current.Item1)) {
                    if((File.GetAttributes(child)&FileAttributes.ReparsePoint)!=0||new[]{"resources","Game","userdata","cache","logs"}.Contains(Path.GetFileName(child),StringComparer.OrdinalIgnoreCase))continue;
                    if(++folders>256)throw new IOException("Choose a smaller package folder.");queue.Enqueue(Tuple.Create(child,current.Item2+1));
                }
            }
            return result.OrderBy(p=>p,StringComparer.OrdinalIgnoreCase).ToList();
        }
        string NewStage(){string p=Path.Combine(Resources,".import-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(p);return p;}
        internal static void CopyTree(string source,string dest,CancellationToken cancel) {
            Paths.CheckParents(source);Paths.CheckParents(dest);Directory.CreateDirectory(dest);
            foreach(string p in Directory.EnumerateFiles(source)){cancel.ThrowIfCancellationRequested();Paths.CheckParents(p);File.Copy(p,Path.Combine(dest,Path.GetFileName(p)),false);}
            foreach(string p in Directory.EnumerateDirectories(source)){cancel.ThrowIfCancellationRequested();CopyTree(p,Path.Combine(dest,Path.GetFileName(p)),cancel);}
        }
        void AddManifest(string stage,string data,Dictionary<string,string> replacements) {
            var manifest=new ImportManifest();foreach(string p in Directory.GetFiles(data,"*",SearchOption.AllDirectories))manifest.files.Add(new ImportedFile{path=p.Substring(data.Length+1).Replace('\\','/'),bytes=new FileInfo(p).Length});
            string file=Path.Combine(stage,"manifest.json");WriteJson(file,manifest);replacements.Add("import-manifest.json",file);
        }
        void FreshConfig(string stage,Dictionary<string,string> replacements) {
            if(replacements.ContainsKey("outrun.toml")||File.Exists(Path.Combine(Resources,"outrun.toml")))return;
            string file=Path.Combine(stage,"config.toml");File.Copy(Path.Combine(Resources,"defaults.toml"),file);replacements.Add("outrun.toml",file);
        }
        void Commit(string stage,Dictionary<string,string> replacements,CancellationToken cancel) {
            var journal=new ImportJournal{stage=Path.GetFileName(stage)};int i=0;
            foreach(var pair in replacements)journal.moves.Add(new ImportMove{source=pair.Value.Substring(stage.Length+1),target=pair.Key,backup="backup/"+(i++),existed=Exists(Paths.Child(Resources,pair.Key))});
            cancel.ThrowIfCancellationRequested();WriteJson(JournalPath,journal);
            // Cancellation ends here: finish or recover this short durable commit.
            try {i=0;foreach(var op in journal.moves) {
                string target=Paths.Child(Resources,op.target),source=Paths.Child(stage,op.source),backup=Paths.Child(stage,op.backup);
                if(op.existed)Move(target,backup);Move(source,target);
#if OUTRUN_PORTABLE_TESTS
                if(AfterMoveForTest!=null)AfterMoveForTest(++i);
#endif
            }journal.committed=true;WriteJson(JournalPath,journal);}catch{RecoverLocked();throw;}
            RecoverLocked();
        }
        bool AllowedTarget(string name){return new[]{"assets","userdata","cache","logs","outrun.toml","pc-settings.toml","multiplayer-settings.toml","import-manifest.json"}.Contains(name)||Regex.IsMatch(name,@"^outrun\.toml\.before-lan-[A-Za-z0-9-]+\.bak$");}
        void RecoverLocked() {
            if(!File.Exists(JournalPath))return;var j=json.Deserialize<ImportJournal>(File.ReadAllText(JournalPath));Guid id;
            if(j==null||j.stage==null||j.stage.Length!=40||!j.stage.StartsWith(".import-")||!Guid.TryParseExact(j.stage.Substring(8),"N",out id)||j.moves==null)throw new IOException("Invalid import journal. Keep resources intact and report the error.");
            string stage=Paths.Child(Resources,j.stage);
            foreach(var op in j.moves){if(!AllowedTarget(op.target))throw new IOException("Invalid journal target.");Paths.Child(Resources,op.target);Paths.Child(stage,op.source);Paths.Child(stage,op.backup);}
            if(!j.committed)foreach(var op in j.moves.AsEnumerable().Reverse()) {
                string target=Paths.Child(Resources,op.target),source=Paths.Child(stage,op.source),backup=Paths.Child(stage,op.backup);
                if(Exists(backup)){if(Exists(target))Remove(target);Move(backup,target);}else if(!op.existed&&!Exists(source)&&Exists(target))Remove(target);
            }
            if(Directory.Exists(stage))Remove(stage);File.Delete(JournalPath);
        }
        internal void Recover(){using(Lock()){RecoverLocked();foreach(string p in Directory.GetDirectories(Resources,".import-*")){Guid id;string n=Path.GetFileName(p);if(n.Length==40&&Guid.TryParseExact(n.Substring(8),"N",out id))Remove(p);}}}
        internal void CheckNotRunning() {
            foreach(var p in Process.GetProcessesByName("outrun"))using(p){try{if(String.Equals(Path.GetDirectoryName(p.MainModule.FileName),Resources,StringComparison.OrdinalIgnoreCase))throw new IOException("Close this OutRun copy before changing game files.");}catch(System.ComponentModel.Win32Exception){throw new IOException("Close OutRun before importing or changing settings.");}}
        }
        internal void Import(string packagePath,Action<int,string> report,CancellationToken cancel) {
            using(Lock()) {
                CheckNotRunning();RecoverLocked();if(Ready())throw new IOException("Game files are already imported. Your working installation has been preserved.");
                ValidatePackage(packagePath);cancel.ThrowIfCancellationRequested();string stage=NewStage();
                try {
                    var replacements=new Dictionary<string,string>();string data=Path.Combine(stage,"assets");
                    using(var package=new StfsPackage(packagePath,false))package.Extract(data,(p,s)=>report(p*90/100,"Importing game files"),cancel);
                    GameValidation.Check(data);replacements.Add("assets",data);FreshConfig(stage,replacements);AddManifest(stage,data,replacements);
                    report(95,"Finishing import…");Commit(stage,replacements,cancel);report(100,"Ready to play.");
                }finally{if(!File.Exists(JournalPath)&&Directory.Exists(stage))Remove(stage);}
            }
        }
        internal string LegacyResources(string folder) {
            foreach(string p in new[]{Path.Combine(folder,"Game"),Path.Combine(folder,"resources"),folder})if(File.Exists(Path.Combine(p,"assets","default.xex")))return Path.GetFullPath(p);return null;
        }
        internal List<string> NearbyLegacy() {
            var list=new List<string>();foreach(string root in new[]{Root,Path.GetDirectoryName(Root),Path.Combine(Path.GetDirectoryName(Root),"OutRun Online Arcade Setup")}){string p=LegacyResources(root);if(p!=null&&!String.Equals(p,Resources,StringComparison.OrdinalIgnoreCase)&&!list.Contains(p))list.Add(p);}return list;
        }
        internal void Migrate(string source,Action<int,string> report,CancellationToken cancel) {
            using(Lock()) {
                CheckNotRunning();RecoverLocked();if(Ready())throw new IOException("This installation is already ready. Migration will not overwrite it.");
                source=LegacyResources(source);if(source==null)throw new IOException("Drop the existing installation folder containing Game/assets.");Paths.CheckParents(source);
                if(String.Equals(source,Resources,StringComparison.OrdinalIgnoreCase))throw new IOException("Choose the old installation, not this resources folder.");
                GameValidation.Check(Path.Combine(source,"assets"));string stage=NewStage();
                try {
                    var replacements=new Dictionary<string,string>();string data=Path.Combine(stage,"assets");report(5,"Copying existing game files…");
                    CopyTree(Path.Combine(source,"assets"),data,cancel);GameValidation.Check(data);replacements.Add("assets",data);
                    var names=new List<string>{"userdata","cache","logs","outrun.toml","pc-settings.toml","multiplayer-settings.toml"};
                    names.AddRange(Directory.GetFiles(source,"outrun.toml.before-lan-*.bak").Select(Path.GetFileName));
                    foreach(string name in names) {
                        string old=Path.Combine(source,name),dest=Path.Combine(Resources,name),copy=Path.Combine(stage,name);
                        if(!Exists(old))continue;if(Exists(dest))throw new IOException("Both locations contain "+name+". Choose a fresh portable folder to preserve both.");
                        if(Directory.Exists(old))CopyTree(old,copy,cancel);else File.Copy(old,copy,false);replacements.Add(name,copy);
                    }
                    FreshConfig(stage,replacements);AddManifest(stage,data,replacements);report(95,"Finishing migration…");Commit(stage,replacements,cancel);report(100,"Ready to play. Old installation preserved.");
                }finally{if(!File.Exists(JournalPath)&&Directory.Exists(stage))Remove(stage);}
            }
        }
        internal void ApplyExtras(bool fullscreen,bool shortcut,string desktop=null) {
            if(!fullscreen&&!shortcut)return;using(Lock()) {
                CheckNotRunning();RecoverLocked();if(!Ready())throw new IOException("Import the game before applying Extras.");
                string link=Path.Combine(desktop??Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),"OutRun Online Arcade.lnk");
                if(shortcut&&Exists(link))throw new IOException("An OutRun shortcut already exists. It was preserved. Uncheck Create desktop shortcut to play.");
                if(fullscreen) {
                    string config=Path.Combine(Resources,"outrun.toml"),text=File.ReadAllText(config);
                    var pattern=new Regex(@"(?m)^[ \t]*fullscreen[ \t]*=[^\r\n]*");if(pattern.Matches(text).Count>1)throw new IOException("Duplicate fullscreen setting.");
                    text=pattern.IsMatch(text)?pattern.Replace(text,"fullscreen = true"):"fullscreen = true\n"+text;
                    string stage=NewStage();try {
                        var replacements=new Dictionary<string,string>();string next=Path.Combine(stage,"config.toml");File.WriteAllText(next,text,new UTF8Encoding(false));replacements.Add("outrun.toml",next);
                        // Native settings override the runtime TOML; update both atomically.
                        string pc=Path.Combine(Resources,"pc-settings.toml");if(File.Exists(pc)) {
                            string settings=File.ReadAllText(pc);if(pattern.Matches(settings).Count!=1)throw new IOException("Invalid native fullscreen setting.");
                            string f=Path.Combine(stage,"pc.toml");File.WriteAllText(f,pattern.Replace(settings,"fullscreen = true"),new UTF8Encoding(false));replacements.Add("pc-settings.toml",f);
                        }
                        Commit(stage,replacements,CancellationToken.None);
                    }finally{if(!File.Exists(JournalPath)&&Directory.Exists(stage))Remove(stage);}
                }
                if(shortcut)CreateShortcut(link);
            }
        }
        // Save & user data folder. Default: resources\userdata. A different folder is
        // stored as one UTF-8 line in resources\userdata-location.txt, which the
        // runtime reads at start-up (src/userdata_location.h). Saves are copied,
        // never moved: the previous folder is always left as it was.
        internal string DefaultUserData {get{return Path.Combine(Resources,"userdata");}}
        string UserDataLocationPath {get{return Path.Combine(Resources,"userdata-location.txt");}}
        internal string UserData {get{
            try {
                if(File.Exists(UserDataLocationPath)) {
                    string chosen=File.ReadAllText(UserDataLocationPath,Encoding.UTF8).Trim().TrimStart('﻿');
                    if(chosen.Length>0&&Path.IsPathRooted(chosen))return Path.GetFullPath(chosen).TrimEnd(Path.DirectorySeparatorChar);
                }
            }catch(IOException){}catch(ArgumentException){}catch(NotSupportedException){}
            return DefaultUserData;
        }}
        internal bool IsDefaultUserData(string path){return SamePath(path,DefaultUserData);}
        static bool SamePath(string a,string b){return String.Equals(Path.GetFullPath(a).TrimEnd(Path.DirectorySeparatorChar),Path.GetFullPath(b).TrimEnd(Path.DirectorySeparatorChar),StringComparison.OrdinalIgnoreCase);}
        static bool Inside(string path,string folder){string p=Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar,f=Path.GetFullPath(folder).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar;return p.StartsWith(f,StringComparison.OrdinalIgnoreCase);}
        static bool HasFiles(string folder){return Directory.Exists(folder)&&Directory.EnumerateFiles(folder,"*",SearchOption.AllDirectories).Any();}
        internal static string CheckUserDataFolder(string path,string resources,string defaultUserData) {
            if(String.IsNullOrWhiteSpace(path)||!Path.IsPathRooted(path))throw new IOException("Choose a full folder path for saves and user data.");
            path=Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar);
            if(Path.GetPathRoot(path).TrimEnd(Path.DirectorySeparatorChar).Equals(path,StringComparison.OrdinalIgnoreCase))throw new IOException("Choose a folder, not the top of a drive.");
            if(!SamePath(path,defaultUserData)&&(Inside(path,resources)||Inside(resources,path)))throw new IOException("Choose a folder outside this game's resources folder, or use Default.");
            return path;
        }
        // Returns a short note on what happened, for the status line.
        internal string SetUserData(string target) {
            using(Lock()) {
                CheckNotRunning();RecoverLocked();
                target=CheckUserDataFolder(target,Resources,DefaultUserData);string current=UserData;
                if(SamePath(target,current))return null;
                Directory.CreateDirectory(target);string note;
                if(HasFiles(target))note="Using the saves already in "+target+".";
                else if(HasFiles(current)){CopyTree(current,target,CancellationToken.None);note="Saves copied to "+target+". The old folder was kept.";}
                else note="Saves will be stored in "+target+".";
                if(IsDefaultUserData(target)){if(File.Exists(UserDataLocationPath))File.Delete(UserDataLocationPath);}
                else {
                    string temp=UserDataLocationPath+".tmp-"+Guid.NewGuid().ToString("N");
                    try{File.WriteAllText(temp,target+"\r\n",new UTF8Encoding(false));if(!MoveFileEx(temp,UserDataLocationPath,1|8))throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());}
                    finally{if(File.Exists(temp))File.Delete(temp);}
                }
                return note;
            }
        }
        void CreateShortcut(string link) {
            Paths.CheckParents(link);Directory.CreateDirectory(Path.GetDirectoryName(link));string temporary=Path.Combine(Path.GetDirectoryName(link),".outrun-"+Guid.NewGuid().ToString("N")+".lnk");object shell=null,shortcut=null;
            try {
                Type type=Type.GetTypeFromProgID("WScript.Shell");shell=Activator.CreateInstance(type);shortcut=type.InvokeMember("CreateShortcut",System.Reflection.BindingFlags.InvokeMethod,null,shell,new object[]{temporary});
                string exe=Path.Combine(Root,"OutRun Online Arcade.exe");foreach(var item in new Dictionary<string,string>{{"TargetPath",exe},{"WorkingDirectory",Root},{"IconLocation",exe+",0"},{"Description","OutRun Online Arcade"}})shortcut.GetType().InvokeMember(item.Key,System.Reflection.BindingFlags.SetProperty,null,shortcut,new object[]{item.Value});
                shortcut.GetType().InvokeMember("Save",System.Reflection.BindingFlags.InvokeMethod,null,shortcut,null);File.Move(temporary,link);
            }finally{if(shortcut!=null)Marshal.FinalReleaseComObject(shortcut);if(shell!=null)Marshal.FinalReleaseComObject(shell);if(File.Exists(temporary))File.Delete(temporary);}
        }
        internal ProcessStartInfo LaunchInfo() {
            if(!Ready())throw new IOException("Browse for your original game package first.");
            string runtime=Path.Combine(Resources,"outrun.exe");if(!File.Exists(runtime))throw new IOException("Extract the complete portable ZIP including resources.");
            return new ProcessStartInfo(runtime){WorkingDirectory=Resources,UseShellExecute=false};
        }
        internal void Launch(){using(Lock()){RecoverLocked();Process.Start(LaunchInfo());}}
    }
}
