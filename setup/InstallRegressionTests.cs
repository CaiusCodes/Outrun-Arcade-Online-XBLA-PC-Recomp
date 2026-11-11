using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace Outrun.Setup {
    internal static class InstallRegressionTests {
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
        static extern SafeFileHandle CreateFile(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
        static SafeFileHandle HoldDirectory(string path) {
            var handle=CreateFile(path,0x80000000,3,IntPtr.Zero,3,0x02000000,IntPtr.Zero);
            if(handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            return handle;
        }
        public static int Main(string[] args) {
            try {
                string root=Path.GetFullPath(args[1]); Directory.CreateDirectory(root);
                string old=Path.Combine(root,"old-staging"); Directory.CreateDirectory(old);
                using(var hold=HoldDirectory(old)) {
                    bool denied=false;
                    try {Directory.Move(old,Path.Combine(root,"old-final"));}
                    catch(IOException) {denied=true;} catch(UnauthorizedAccessException) {denied=true;}
                    if(!denied) throw new Exception("The regression did not reproduce directory rename denial.");
                }
                Console.WriteLine("PASS: a directory handle without delete sharing reproduces the old rename failure.");
                string target=Path.Combine(root,"locked-final");
                SafeFileHandle destinationLock=null;
                int last=0;
                try {
                    InstallEngine.Install(args[0],target,(p,s)=> {
                        if(destinationLock==null) destinationLock=HoldDirectory(target);
                        if(p<last) throw new Exception("Progress moved backwards.");
                        if(p==100 && (!File.Exists(Path.Combine(target,"Game","installation-complete.txt")) || File.Exists(Path.Combine(target,"INSTALLATION-INCOMPLETE.txt")))) throw new Exception("100% reported before completion.");
                        last=p;
                    });
                } finally {if(destinationLock!=null)destinationLock.Dispose();}
                if(last!=100 || !File.Exists(Path.Combine(target,InstallEngine.LauncherName))) throw new Exception("Installation did not publish a launcher.");
                Console.WriteLine("PASS: installation succeeds while the final directory is locked against rename; 100% comes after completion.");
                target=Path.Combine(root,"validation-failure");
                FileStream imageLock=null;
                bool failed=false;
                try {
                    InstallEngine.Install(args[0],target,(p,s)=> {
                        if(p==96 && imageLock==null)imageLock=new FileStream(Path.Combine(target,"Game","assets","default.xex"),FileMode.Open,FileAccess.Read,FileShare.None);
                    });
                } catch(IOException){failed=true;}
                finally {if(imageLock!=null)imageLock.Dispose();}
                if(!failed || File.Exists(Path.Combine(target,InstallEngine.LauncherName)) || File.Exists(Path.Combine(target,"Game","installation-complete.txt")) || !File.Exists(Path.Combine(target,"INSTALLATION-INCOMPLETE.txt")) || !File.Exists(Path.Combine(target,"Game","setup-error.txt")))throw new Exception("An incomplete install appeared complete.");
                Console.WriteLine("PASS: a validation failure leaves diagnostics and an incomplete marker, with no Play launcher.");
                return 0;
            } catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
        }
    }
}
