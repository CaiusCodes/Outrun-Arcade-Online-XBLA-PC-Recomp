using System;
using System.IO;
using System.Threading;

namespace Outrun.Setup {
    internal static class ImportCli {
        public static int Main(string[] args) {
            try {
                if (args.Length != 2) throw new ArgumentException("Usage: ImportOutrun.exe <Xbox 360 package> <new output folder>");
                string target = Path.GetFullPath(args[1]);
                Paths.CheckParents(target);
                if (Directory.Exists(target) || File.Exists(target)) throw new IOException("Output already exists. Choose a new folder to preserve existing data.");
                using (var package = new StfsPackage(args[0], false)) {
                    Console.WriteLine("Importing " + package.DisplayName);
                    Console.WriteLine("Asset bytes: " + package.TotalBytes);
                    package.Extract(target, (percent, file) => { if (percent % 10 == 0) Console.WriteLine(percent + "% " + file); }, CancellationToken.None);
                }
                string xex = Path.Combine(target, "default.xex");
                using (var stream = File.OpenRead(xex)) {
                    byte[] magic = new byte[4];
                    if (stream.Read(magic, 0, 4) != 4 || System.Text.Encoding.ASCII.GetString(magic) != "XEX2") throw new InvalidDataException("The package does not contain a valid default.xex.");
                }
                GameValidation.Check(target);
                Console.WriteLine("Import complete: " + target);
                return 0;
            } catch (Exception ex) { Console.Error.WriteLine(ex.Message); return 1; }
        }
    }
}
