using System;
using System.Threading;
namespace Outrun.Setup {
    internal static class PortableSmoke {
        static int Main(string[] args) {
            try{var game=new PortableGame(args[0]);game.Migrate(args[1],(p,s)=>{},CancellationToken.None);Console.WriteLine("PASS: actual older Game installation copied; Ready="+game.Ready());return 0;}
            catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
        }
    }
}
