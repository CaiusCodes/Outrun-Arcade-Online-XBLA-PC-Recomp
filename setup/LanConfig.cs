using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace Outrun.Setup {
    internal static class LanConfig {
        internal static string Address(string value) {
            string[] parts=value.Trim().Split('.');
            byte number;
            if(parts.Length!=4 || parts.Any(p=>p.Length==0 || p.Any(c=>c<'0'||c>'9') || !Byte.TryParse(p,out number)))
                throw new ArgumentException("Enter an IPv4 address, for example 26.12.34.56.");
            var bytes=parts.Select(Byte.Parse).ToArray();
            if(bytes[0]==0 || bytes[0]==127 || bytes[0]>=224 || (bytes[0]==169 && bytes[1]==254))
                throw new ArgumentException("Choose a LAN or virtual-LAN address, not a loopback or link-local address.");
            return String.Join(".",bytes.Select(b=>b.ToString()).ToArray());
        }
        internal static string Update(string text,string local,string host,bool hosting) {
            local=Address(local); host=hosting ? "" : Address(host);
            if(host==local)throw new ArgumentException("The host address must belong to the other PC.");
            var values=new Dictionary<string,string> {
                {"net_address","\""+local+"\""}, {"net_join_address","\""+host+"\""},
                {"net_private_match","true"}, {"outrun_local_sessions","true"}, {"net_relay_server","\"\""}
            };
            string newline=text.Contains("\r\n") ? "\r\n" : "\n";
            foreach(var pair in values) {
                string pattern=@"(?m)^[ \t]*"+Regex.Escape(pair.Key)+@"[ \t]*=[^\r\n]*";
                if(Regex.Matches(text,pattern).Count>1)throw new IOException("Duplicate network setting: "+pair.Key);
                string line=pair.Key+" = "+pair.Value;
                if(Regex.IsMatch(text,pattern))text=Regex.Replace(text,pattern,line);
                else text=text.TrimEnd('\r','\n')+newline+line+newline;
            }
            return text;
        }
        internal static string Read(string text,string key) {
            var match=Regex.Match(text,@"(?m)^[ \t]*"+Regex.Escape(key)+"[ \\t]*=[ \\t]*\"([^\"]*)\"");
            return match.Success ? match.Groups[1].Value : "";
        }
        internal static void Save(string path,string local,string host,bool hosting) {
            string original=File.ReadAllText(path);
            string next=Update(original,local,host,hosting);
            string temp=path+".tmp-"+Guid.NewGuid().ToString("N");
            string backup=path+".before-lan-"+DateTime.UtcNow.ToString("yyyyMMdd-HHmmss")+"-"+Guid.NewGuid().ToString("N")+".bak";
            try {
                using(var file=new FileStream(temp,FileMode.CreateNew,FileAccess.Write,FileShare.None)) {
                    byte[] bytes=new UTF8Encoding(false).GetBytes(next); file.Write(bytes,0,bytes.Length);file.Flush(true);
                }
                if(File.ReadAllText(path)!=original)throw new IOException("Settings changed while saving. Reopen Configure LAN and try again.");
                File.Replace(temp,path,backup);
            } finally {if(File.Exists(temp))File.Delete(temp);}
        }
    }
    internal sealed class LanForm:Form {
        readonly ComboBox local=new ComboBox();
        readonly TextBox host=new TextBox();
        readonly RadioButton hosting=new RadioButton(),joining=new RadioButton();
        readonly Label status=new Label();
        readonly string config;
        internal LanForm(string root):this(root,false){}
        internal LanForm(string root,bool portable) {
            SuspendLayout();
            Text="OutRun Online Arcade | Configure LAN";
            AutoScaleDimensions=new SizeF(96,96);AutoScaleMode=AutoScaleMode.Dpi;
            Font=new Font("Segoe UI",10);BackColor=Color.FromArgb(250,248,240);ForeColor=Color.FromArgb(18,51,66);
            ClientSize=new Size(660,440);FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;
            StartPosition=FormStartPosition.CenterScreen;Icon=Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            config=Path.Combine(root,portable?"resources":"Game","outrun.toml");
            Controls.Add(new Label {Text="LAN setup",Location=new Point(28,22),Size=new Size(590,42),Font=new Font("Segoe UI",22,FontStyle.Bold)});
            Controls.Add(new Label {Text="Play OutRun via LAN or virtual LAN networks.",Location=new Point(30,75),Size=new Size(600,25)});
            hosting.Text="Host a game";hosting.Location=new Point(30,115);hosting.Size=new Size(180,30);
            joining.Text="Join a game";joining.Location=new Point(250,115);joining.Size=new Size(180,30);
            Controls.Add(hosting);Controls.Add(joining);
            Controls.Add(new Label {Text="Your local address",Location=new Point(30,162),Size=new Size(590,25)});
            local.Location=new Point(30,192);local.Size=new Size(600,32);local.DropDownStyle=ComboBoxStyle.DropDownList;
            foreach(var adapter in NetworkInterface.GetAllNetworkInterfaces()) {
                if(adapter.OperationalStatus!=OperationalStatus.Up)continue;
                foreach(var ip in adapter.GetIPProperties().UnicastAddresses) {
                    if(ip.Address.AddressFamily!=AddressFamily.InterNetwork)continue;
                    try {local.Items.Add(new LocalAddress(AddressText(ip.Address),adapter.Name));}catch(ArgumentException){}
                }
            }
            Controls.Add(local);
            Controls.Add(new Label {Text="Host address (joining only)",Location=new Point(30,238),Size=new Size(590,25)});
            host.Location=new Point(30,268);host.Size=new Size(600,32);Controls.Add(host);
            local.AccessibleName="Your local address";host.AccessibleName="Host address";
            hosting.CheckedChanged+=(s,e)=>host.Enabled=!hosting.Checked;
            status.Location=new Point(30,311);status.Size=new Size(600,50);
            status.Text="Choose your virtual-LAN adapter when playing over a virtual LAN.";Controls.Add(status);
            var save=new Button {Text="Save",Location=new Point(460,373),Size=new Size(170,42),BackColor=Color.FromArgb(217,63,51),ForeColor=Color.White,FlatStyle=FlatStyle.Flat};
            save.FlatAppearance.BorderSize=0;save.Click+=Save;Controls.Add(save);AcceptButton=save;
            var cancel=new Button {Text="Close",Location=new Point(275,373),Size=new Size(170,42)};
            cancel.Click+=(s,e)=>Close();Controls.Add(cancel);CancelButton=cancel;
            string text=File.Exists(config)?File.ReadAllText(config):"";
            string selected=LanConfig.Read(text,"net_address");host.Text=LanConfig.Read(text,"net_join_address");
            for(int i=0;i<local.Items.Count;++i)if(((LocalAddress)local.Items[i]).Address==selected)local.SelectedIndex=i;
            if(local.SelectedIndex<0 && local.Items.Count==1)local.SelectedIndex=0;
            hosting.Checked=String.IsNullOrEmpty(host.Text);joining.Checked=!hosting.Checked;
            ResumeLayout(false);PerformLayout();
        }
        static string AddressText(IPAddress address){return LanConfig.Address(address.ToString());}
        sealed class LocalAddress {
            internal readonly string Address;readonly string adapter;
            internal LocalAddress(string address,string name){Address=address;adapter=name;}
            public override string ToString(){return Address+"  —  "+adapter;}
        }
        void Save(object sender,EventArgs e) {
            try {
                if(!File.Exists(Path.Combine(Path.GetDirectoryName(config),"installation-complete.txt")) && !File.Exists(Path.Combine(Path.GetDirectoryName(config),"import-manifest.json")))throw new IOException("Import the game in this folder first.");
                // Changing addresses while a game runs can change its identity/route.
                if(Process.GetProcessesByName("outrun").Length>0)throw new IOException("Close OutRun before saving the network settings.");
                var selected=local.SelectedItem as LocalAddress;
                if(selected==null)throw new ArgumentException("Choose your LAN or virtual-LAN adapter address.");
                LanConfig.Save(config,selected.Address,host.Text,hosting.Checked);
                status.ForeColor=Color.FromArgb(0,110,100);
                status.Text=hosting.Checked ? "Saved. Start OutRun and choose Multiplayer > Host LAN Match." : "Saved. Start OutRun and choose Multiplayer > LAN Match > Quick Match.";
            }catch(Exception ex){status.ForeColor=Color.FromArgb(190,40,30);status.Text=ex.Message;}
        }
        internal void Preview() {
            local.Items.Clear();local.Items.Add(new LocalAddress("26.10.20.30","Virtual LAN (example)"));local.SelectedIndex=0;
            joining.Checked=true;host.Text="26.10.20.31";
        }
        [STAThread] static int Main(string[] args) {
            Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
            if(args.Length==3 && args[0]=="--render-preview") {
                try {using(var form=new LanForm(AppDomain.CurrentDomain.BaseDirectory)) {
                    form.Preview();SetupPreview.Render(form,Path.GetFullPath(args[1]),Int32.Parse(args[2]));
                }return 0;}catch(Exception ex){File.WriteAllText(args[1]+".error.txt",ex.ToString());return 1;}
            }
            try {Application.Run(new LanForm(AppDomain.CurrentDomain.BaseDirectory));return 0;}
            catch(Exception ex){MessageBox.Show(ex.Message,"Configure LAN",MessageBoxButtons.OK,MessageBoxIcon.Error);return 1;}
        }
    }
}
