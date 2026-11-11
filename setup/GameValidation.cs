using System;
using System.IO;
using System.Security.Cryptography;
namespace Outrun.Setup {
    internal static class GameValidation {
        internal const string XexSha256 = "1E97C08AF72DD19F674A03DA1B7DC15C8825EC2283F985D71DCC92B97B6775F0";
        internal static void Check(string assets) {
            using (var stream = File.OpenRead(Path.Combine(assets, "default.xex")))
            using (var sha = SHA256.Create()) {
                if (BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "") != XexSha256)
                    throw new InvalidDataException("This executable version is not supported by this development build. Expected OutRun Online Arcade 0.0.1.5.");
            }
        }
    }
}
