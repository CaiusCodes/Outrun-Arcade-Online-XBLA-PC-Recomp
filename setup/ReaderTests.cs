using System;
using System.IO;
using System.Linq;
namespace Outrun.Setup {
    internal static class ReaderTests {
        static int count;
        static void Reject(Action action, string message) {
            try { action(); } catch (InvalidDataException) { count++; return; }
            throw new Exception("Expected rejection: " + message);
        }
        static byte[] Header() {
            byte[] b = new byte[0xD000];
            Array.Copy(System.Text.Encoding.ASCII.GetBytes("LIVE"), b, 4);
            b[0x341]=0; b[0x342]=0xB0; b[0x343]=0;
            b[0x345]=0x0D;
            b[0x360]=0x58; b[0x361]=0x41; b[0x362]=0x09; b[0x363]=0x68;
            b[0x37B]=1; b[0x37C]=1;
            return b;
        }
        public static int Main(string[] args) {
            try {
                string root=Path.GetFullPath(args[0]); Directory.CreateDirectory(root);
                foreach(string name in new[]{"..","CON.txt","a:b","a/b","a\\b","trailing.","trailing "})
                    Reject(()=>Paths.CheckName(name),name);
                Reject(()=>Paths.Child(root,"..\\outside"),"traversal");
                string path=Path.Combine(root,"wrong-title.stfs");
                byte[] wrong=Header(); wrong[0x363]=0x6A; File.WriteAllBytes(path,wrong);
                Reject(()=>{using(var p=new StfsPackage(path,false)){}},"wrong title");
                path=Path.Combine(root,"truncated.stfs"); File.WriteAllBytes(path,new byte[4]);
                Reject(()=>{using(var p=new StfsPackage(path,false)){}},"short header");
                path=Path.Combine(root,"traversal.stfs");
                byte[] traversal=Header(); traversal[0xC000]=46; traversal[0xC001]=46; traversal[0xC028]=0x82;
                File.WriteAllBytes(path,traversal);
                Reject(()=>{using(var p=new StfsPackage(path,false)){}},"unsafe filename in real table");
                Console.WriteLine("PASS: " + count + " invalid input checks"); return 0;
            } catch(Exception ex) {Console.Error.WriteLine(ex); return 1;}
        }
    }
}
