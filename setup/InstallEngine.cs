using System;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Threading;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace Outrun.Setup {
    internal static class InstallEngine {
        internal const string LauncherName = "OutRun Online Arcade.exe";
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        static extern bool CreateDirectory(string path, IntPtr securityAttributes);
        static void WriteEntry(ZipArchiveEntry entry, string root) {
            string file = Paths.Child(root, entry.FullName);
            Directory.CreateDirectory(Path.GetDirectoryName(file));
            using (var input = entry.Open())
            using (var output = new FileStream(file, FileMode.CreateNew)) input.CopyTo(output);
        }
        internal static void Install(string packagePath, string destination, Action<int,string> progress) {
            if (String.IsNullOrWhiteSpace(destination)) throw new IOException("Choose an installation folder first.");
            string target = Path.GetFullPath(destination).TrimEnd(Path.DirectorySeparatorChar);
            Paths.CheckParents(target);
            if (File.Exists(target)) throw new IOException("The installation path is a file. Choose a folder.");
            // The extracted setup directory is a valid destination. Reserve only
            // the game's paths; setup, licenses, resources and user files stay intact.
            foreach (string name in new[] { "Game", LauncherName, "Configure LAN.exe", "INSTALLATION-INCOMPLETE.txt" }) {
                string path = Paths.Child(target, name);
                if (Directory.Exists(path) || File.Exists(path))
                    throw new IOException("This folder already contains game files or an unfinished installation (" + name + "). Existing game data and saves are preserved. Choose another folder.");
            }
            string parent = Path.GetDirectoryName(target);
            if (String.IsNullOrEmpty(parent)) throw new IOException("Choose a folder below a drive root.");
            using (var package = new StfsPackage(packagePath, false)) {
                Directory.CreateDirectory(target);
                // Atomically claim only Game. This also prevents concurrent installs
                // from merging even when both selected the same existing setup folder.
                if (!CreateDirectory(Path.Combine(target, "Game"), IntPtr.Zero)) throw new IOException("Cannot create a new Game folder: " + target, new Win32Exception(Marshal.GetLastWin32Error()));
                string phase = "Preparing the installation";
                try {
                    progress(1, phase);
                    using (var marker = new StreamWriter(new FileStream(Path.Combine(target, "INSTALLATION-INCOMPLETE.txt"), FileMode.CreateNew)))
                        marker.WriteLine("Installation has not finished. The Play launcher will be added only after validation.");
                    using (var payload = Assembly.GetExecutingAssembly().GetManifestResourceStream("Outrun.Payload")) {
                        if (payload == null) throw new IOException("Installer payload is missing.");
                        using (var zip = new ZipArchive(payload, ZipArchiveMode.Read)) {
                            foreach (var entry in zip.Entries) {
                                if (entry.FullName == LauncherName) continue;
                                // The ZIP's existing README describes setup. Keep it.
                                if (entry.FullName == "README.txt" && (File.Exists(Path.Combine(target, "README.txt")) || Directory.Exists(Path.Combine(target, "README.txt")))) continue;
                                WriteEntry(entry, target);
                            }
                        }
                    }
                    phase = "Importing game files";
                    string assets = Path.Combine(target, "Game", "assets");
                    package.Extract(assets, (p,f) => progress(5 + p*90/100, "Importing game files"), CancellationToken.None);
                    phase = "Checking the game version";
                    progress(96, phase);
                    GameValidation.Check(assets);
                    File.WriteAllText(Path.Combine(target, "Game", "import-receipt.txt"), "OutRun Online Arcade | 58410968 | 0.0.1.5\r\nXEX SHA256: " + GameValidation.XexSha256 + "\r\n");
                    phase = "Finishing setup";
                    progress(98, phase);
                    // Publish the launcher last. A directory held by a sync/indexing process
                    // can remain open throughout installation: no populated-directory rename.
                    using (var payload = Assembly.GetExecutingAssembly().GetManifestResourceStream("Outrun.Payload"))
                    using (var zip = new ZipArchive(payload, ZipArchiveMode.Read)) {
                        var launcher = zip.GetEntry(LauncherName);
                        if (launcher == null) throw new IOException("Play launcher is missing from this setup.");
                        WriteEntry(launcher, target);
                    }
                    File.WriteAllText(Path.Combine(target, "Game", "installation-complete.txt"), DateTime.UtcNow.ToString("o"));
                    File.Delete(Path.Combine(target, "INSTALLATION-INCOMPLETE.txt"));
                    progress(100, "Installation complete");
                } catch (Exception ex) {
                    try { File.WriteAllText(Path.Combine(target, "Game", "setup-error.txt"), "Phase: " + phase + "\r\n" + ex); } catch { }
                    throw new IOException(phase + " failed.\r\n" + ex.Message + "\r\nPartial files were kept in: " + target + "\r\nChoose another writable folder to retry. Details: Game/setup-error.txt", ex);
                }
            }
        }
    }
}
