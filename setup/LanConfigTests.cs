using System;
using System.IO;
using System.Linq;
using System.Text;
namespace Outrun.Setup {
    internal static class LanConfigTests {
        static void Check(bool okay,string message){if(!okay)throw new Exception(message);}
        static int Main(string[] args) {
            string root=args[0];Directory.CreateDirectory(root);
            string path=Path.Combine(root,"outrun.toml");
            try {
                string original="# keep comment\nresolution_scale = 2\nnet_player_name = \"Keep Me\"\nnet_address = \"127.0.0.2\"\nnet_join_address = \"127.0.0.1\"\n";
                File.WriteAllText(path,original,new UTF8Encoding(false));
                LanConfig.Save(path,"26.12.34.56","26.12.34.57",false);
                string joined=File.ReadAllText(path);
                Check(LanConfig.Read(joined,"net_address")=="26.12.34.56" && LanConfig.Read(joined,"net_join_address")=="26.12.34.57","Join addresses wrong");
                Check(joined.Contains("resolution_scale = 2") && joined.Contains("net_player_name = \"Keep Me\"") && joined.StartsWith("# keep comment"),"Unrelated settings changed");
                Check(File.ReadAllText(Directory.GetFiles(root,"*.bak").Single())==original,"Backup is not original");
                LanConfig.Save(path,"26.12.34.56","ignored",true);
                Check(LanConfig.Read(File.ReadAllText(path),"net_join_address")=="","Host kept a join target");
                string before=File.ReadAllText(path);
                foreach(string bad in new[]{"","127.0.0.1","localhost","26.2","26.1.2.256","224.1.2.3","0.0.0.0","26.1.2.3\"\nnet_player_name=\"x","169.254.1.2"}) {
                    bool rejected=false;try{LanConfig.Save(path,bad,"26.1.2.4",false);}catch(ArgumentException){rejected=true;}
                    Check(rejected && File.ReadAllText(path)==before,"Bad local input modified file");
                    rejected=false;try{LanConfig.Save(path,"26.1.2.4",bad,false);}catch(ArgumentException){rejected=true;}
                    Check(rejected && File.ReadAllText(path)==before,"Bad host input modified file");
                }
                bool self=false;try{LanConfig.Update(before,"26.1.2.3","26.1.2.3",false);}catch(ArgumentException){self=true;}Check(self,"Self target accepted");
                bool duplicate=false;try{LanConfig.Update(before+"net_address = \"26.1.2.3\"\n","26.1.2.4","",true);}catch(IOException){duplicate=true;}Check(duplicate,"Duplicate setting accepted");
                using(var locked=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)) {
                    bool failed=false;try{LanConfig.Save(path,"26.1.2.5","",true);}catch(IOException){failed=true;}
                    Check(failed && File.ReadAllText(path)==before,"Failed replacement damaged config");
                }
                Check(Directory.GetFiles(root,"*.tmp-*").Length==0,"Temporary config leaked");
                Console.WriteLine("PASS: host/join config, address rejection, unrelated settings, exact backup, locked-file preservation");return 0;
            }catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
        }
    }
}
