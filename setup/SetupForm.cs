using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Windows.Forms;

namespace Outrun.Setup {
    internal sealed class SetupForm : Form {
        readonly TextBox package = new TextBox(), destination = new TextBox();
        readonly Button install = new Button(), browsePackage = new Button(), browseDestination = new Button();
        readonly RouteProgress progress = new RouteProgress();
        readonly Label status = new Label(), percent = new Label();
        readonly CoastalArt art = new CoastalArt();
        readonly Panel content = new Panel();
        bool busy, complete;
        string installedPath;
        static readonly Color Ink = Color.FromArgb(18, 51, 66);
        static readonly Color Muted = Color.FromArgb(80, 104, 113);
        static readonly Color Red = Color.FromArgb(217, 63, 51);
        internal SetupForm() {
            SuspendLayout();
            Text = "OutRun Online Arcade | PC Setup";
            AutoScaleDimensions = new SizeF(96,96); AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(840,480); FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox=false; StartPosition=FormStartPosition.CenterScreen;
            Icon=Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            Font=new Font("Segoe UI",10); BackColor=Color.FromArgb(250,248,240); ForeColor=Ink;
            art.Location=Point.Empty; art.Size=new Size(300,480); Controls.Add(art);
            content.Location=new Point(300,0); content.Size=new Size(540,480); content.BackColor=BackColor; Controls.Add(content);
            AddLabel("PC setup",30,29,470,44,24,FontStyle.Bold,Ink);
            AddLabel("Import your Xbox 360 game package.",32,82,470,25,10,FontStyle.Regular,Muted);
            AddLabel("Game package",32,127,470,23,10,FontStyle.Bold,Ink);
            AddField(package,browsePackage,157);
            AddLabel("Install folder",32,224,470,23,10,FontStyle.Bold,Ink);
            AddField(destination,browseDestination,254);
            AddLabel("Installs in a Game folder here. Saves stay with the game.",32,301,480,24,9,FontStyle.Regular,Muted);
            destination.Text=AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
            package.AccessibleName="Xbox 360 game package"; destination.AccessibleName="Installation folder";
            browsePackage.Click+=(s,e)=> { using(var d=new OpenFileDialog {Title="Choose your OutRun Online Arcade Xbox 360 package",Filter="Xbox 360 package|*.*",RestoreDirectory=true}) if(d.ShowDialog(this)==DialogResult.OK) package.Text=d.FileName; };
            browseDestination.Click+=(s,e)=> { using(var d=new FolderBrowserDialog {Description="Choose the install folder. This can be the folder containing setup.",SelectedPath=destination.Text}) if(d.ShowDialog(this)==DialogResult.OK) destination.Text=d.SelectedPath; };
            progress.Location=new Point(32,343); progress.Size=new Size(476,6); content.Controls.Add(progress);
            status.Location=new Point(32,360); status.Size=new Size(348,22); status.ForeColor=Muted; status.Font=new Font("Segoe UI",9); status.Text="Ready to install."; content.Controls.Add(status);
            percent.Location=new Point(383,360); percent.Size=new Size(125,22); percent.TextAlign=ContentAlignment.TopRight; percent.Font=new Font("Segoe UI",9,FontStyle.Bold); content.Controls.Add(percent);
            install.Text="Install"; install.Location=new Point(312,402); install.Size=new Size(196,44); StyleButton(install,true); content.Controls.Add(install);
            AddLabel("Version 0.9.0",32,404,235,42,9,FontStyle.Regular,Muted);
            install.Click+=BeginInstall;
            FormClosing+=(s,e)=>{if(busy)e.Cancel=true;};
            AcceptButton=install;
            ResumeLayout(false); PerformLayout();
        }
        void AddLabel(string text,int x,int y,int w,int h,float size,FontStyle style,Color color) {
            content.Controls.Add(new Label {Text=text,Location=new Point(x,y),Size=new Size(w,h),Font=new Font("Segoe UI",size,style),ForeColor=color});
        }
        static void StyleButton(Button b,bool primary) {
            b.FlatStyle=FlatStyle.Flat; b.FlatAppearance.BorderSize=primary?0:1;
            b.FlatAppearance.BorderColor=Color.FromArgb(192,205,202);
            b.BackColor=primary?Red:Color.FromArgb(237,239,230); b.ForeColor=primary?Color.White:Ink;
            b.Font=new Font("Segoe UI",10,FontStyle.Bold); b.Cursor=Cursors.Hand;
        }
        void AddField(TextBox box,Button browse,int y) {
            var border=new Panel {Location=new Point(32,y),Size=new Size(356,38),BackColor=Color.FromArgb(202,214,209)};
            var inner=new Panel {Location=new Point(1,1),Size=new Size(354,36),BackColor=Color.White}; border.Controls.Add(inner);
            box.BorderStyle=BorderStyle.None; box.Location=new Point(11,8); box.Size=new Size(332,24); box.ForeColor=Ink; box.Font=new Font("Segoe UI",10); inner.Controls.Add(box); content.Controls.Add(border);
            browse.Text="Browse…"; browse.Location=new Point(398,y); browse.Size=new Size(110,38); StyleButton(browse,false); content.Controls.Add(browse);
        }
        void SetBusy(bool value) {
            busy=value; package.Enabled=destination.Enabled=browsePackage.Enabled=browseDestination.Enabled=install.Enabled=!value;
            install.Text=value?"Installing…":"Install";
        }
        void BeginInstall(object sender,EventArgs e) {
            if(complete) { Process.Start(new ProcessStartInfo("explorer.exe", "\""+installedPath+"\"") {UseShellExecute=true}); return; }
            string input=package.Text.Trim(), target=destination.Text.Trim();
            if(!File.Exists(input)) {status.ForeColor=Red; status.Text="Choose your original game package first."; package.Focus(); return;}
            if(String.IsNullOrWhiteSpace(target)) {status.ForeColor=Red; status.Text="Choose a destination folder first."; destination.Focus(); return;}
            SetBusy(true); status.ForeColor=Muted; status.Text="Preparing installation…"; percent.Text="0%"; progress.Value=0;
            var worker=new BackgroundWorker {WorkerReportsProgress=true};
            worker.DoWork+=(s,a)=>InstallEngine.Install(input,target,(p,f)=>worker.ReportProgress(p,f));
            worker.ProgressChanged+=(s,a)=>{progress.Value=a.ProgressPercentage; status.Text=Convert.ToString(a.UserState); percent.Text=a.ProgressPercentage+"%";};
            worker.RunWorkerCompleted+=(s,a)=>{
                SetBusy(false);
                if(a.Error!=null) {status.ForeColor=Red; status.Text="Installation failed. Your files are safe."; percent.Text="NOT INSTALLED"; MessageBox.Show(this,a.Error.Message,"Couldn't finish setup",MessageBoxButtons.OK,MessageBoxIcon.Error);}
                else {complete=true; installedPath=Path.GetFullPath(target); status.Text="Installation complete."; percent.Text="COMPLETE"; progress.Value=100; install.Text="Open game folder"; package.Enabled=destination.Enabled=browsePackage.Enabled=browseDestination.Enabled=false;}
                worker.Dispose();
            };
            worker.RunWorkerAsync();
        }
        internal void Preview(string state) {

            package.Text="D:\\Xbox 360\\OutRun Online Arcade\\58410968";
            destination.Text="D:\\Games\\OutRun Online Arcade PC";
            if(state=="progress") {SetBusy(true); progress.Value=67; percent.Text="67%"; status.Text="Importing game files";}
            if(state=="complete") {progress.Value=100; percent.Text="COMPLETE"; status.Text="Installation complete."; install.Text="Open game folder";}
            if(state=="error") {progress.Value=95; percent.Text="NOT INSTALLED"; status.ForeColor=Red; status.Text="Installation failed. Your files are safe.";}
        }
    }
    internal sealed class RouteProgress : Control {
        int value;
        internal int Value {get{return value;}set{this.value=Math.Max(0,Math.Min(100,value)); Invalidate();}}
        internal RouteProgress(){DoubleBuffered=true;}
        protected override void OnPaint(PaintEventArgs e) {
            e.Graphics.Clear(Color.FromArgb(219,227,217));
            using(var b=new SolidBrush(Color.FromArgb(0,133,146))) e.Graphics.FillRectangle(b,0,0,Width*value/100,Height);
        }
    }
    internal sealed class CoastalArt : Control {

        internal CoastalArt(){DoubleBuffered=true;}
        static Color C(int r,int g,int b){return Color.FromArgb(r,g,b);}
        static void Polygon(Graphics g,Color c,params PointF[] p){using(var b=new SolidBrush(c))g.FillPolygon(b,p);}
        static void PaintText(Graphics g,string text,float size,FontStyle style,Color c,float x,float y){using(var f=new Font("Segoe UI",size*96f/72f,style,GraphicsUnit.Pixel))using(var b=new SolidBrush(c))g.DrawString(text,f,b,x,y);}
        protected override void OnPaint(PaintEventArgs e) {
            var g=e.Graphics; g.SmoothingMode=SmoothingMode.AntiAlias; g.ScaleTransform(Width/360f,Height/640f);
            Color sky=C(75,203,218), horizon=C(199,240,224);
            using(var gradient=new LinearGradientBrush(new Rectangle(0,0,360,640),sky,horizon,90))g.FillRectangle(gradient,0,0,360,640);
            using(var sun=new SolidBrush(C(255,251,219)))g.FillEllipse(sun,224,197,105,105);
            using(var path=new GraphicsPath()) {
                using(var family=new FontFamily("Arial"))path.AddString("OutRun",family,(int)(FontStyle.Bold|FontStyle.Italic),69,new PointF(15,61),StringFormat.GenericDefault);
                using(var outline=new Pen(Color.FromArgb(225,255,255,255),5)){outline.LineJoin=LineJoin.Round; g.DrawPath(outline,path);}
                using(var fill=new SolidBrush(C(7,65,97)))g.FillPath(fill,path);
            }
            PaintText(g,"O N L I N E   A R C A D E",12,FontStyle.Bold,C(10,77,97),28,141);
            using(var p=new Pen(C(6,128,146),2))g.DrawLine(p,29,175,328,175);
            Polygon(g,C(64,166,175),new PointF(0,306),new PointF(57,245),new PointF(110,293),new PointF(177,266),new PointF(234,305),new PointF(360,288),new PointF(360,399),new PointF(0,399));
            using(var water=new LinearGradientBrush(new Rectangle(0,311,360,329),C(8,165,184),C(1,101,139),90))g.FillRectangle(water,0,311,360,329);
            using(var pen=new Pen(Color.FromArgb(85,240,255,237),1.5f)) {
                g.DrawLine(pen,15,345,85,345);g.DrawLine(pen,27,374,129,374);g.DrawLine(pen,13,415,118,415);g.DrawLine(pen,50,448,157,448);g.DrawLine(pen,10,480,96,480);
            }
            using(var beach=new GraphicsPath()) {
                beach.AddBezier(352,296,192,352,351,375,205,434);beach.AddBezier(205,434,49,510,4,540,-38,640);
                beach.AddLine(-38,640,360,640);beach.AddLine(360,640,360,296);beach.CloseFigure();
                using(var b=new SolidBrush(C(239,221,177)))g.FillPath(b,beach);
            }
            using(var road=new GraphicsPath()) {
                road.AddBezier(349,298,211,354,372,390,237,449);road.AddBezier(237,449,112,508,122,575,73,640);
                road.AddLine(73,640,312,640);road.AddBezier(312,640,261,557,197,530,273,458);road.AddBezier(273,458,393,387,237,348,358,298);road.CloseFigure();
                using(var b=new SolidBrush(C(63,82,86)))g.FillPath(b,road);
                using(var pen=new Pen(C(251,250,228),3))g.DrawPath(pen,road);
            }
            using(var center=new GraphicsPath())using(var pen=new Pen(C(250,239,183),3)){center.AddBezier(353,299,222,352,383,389,255,454);center.AddBezier(255,454,154,523,192,571,190,640);pen.DashPattern=new float[]{5,6};g.DrawPath(pen,center);}
            Palm(g,42,501,0.96f);Palm(g,304,346,0.5f);
            DrawCar(g,203,489);
        }
        static void Palm(Graphics g,float x,float y,float scale) {
            var saved=g.Save(); g.TranslateTransform(x,y);g.ScaleTransform(scale,scale);
            using(var trunk=new Pen(C(78,97,71),10)){g.DrawBezier(trunk,0,0,10,-38,-2,-90,21,-137);}
            for(int i=0;i<7;i++) {
                double a=(-170+i*28)*Math.PI/180;float dx=(float)Math.Cos(a)*75,dy=(float)Math.Sin(a)*36;
                using(var leaf=new GraphicsPath()){leaf.AddBezier(21,-137,21+dx*0.4f,-165+dy,21+dx,-145+dy,21+dx,-121+dy);leaf.AddBezier(21+dx,-121+dy,21+dx*.7f,-146+dy,29,-141,21,-137);using(var b=new SolidBrush(i%2==0?C(15,101,93):C(14,123,98)))g.FillPath(b,leaf);}
            }
            g.Restore(saved);
        }
        static void DrawCar(Graphics g,float x,float y) {
            var s=g.Save();g.TranslateTransform(x,y);g.RotateTransform(-13);
            using(var shadow=new SolidBrush(Color.FromArgb(80,0,0,0)))g.FillEllipse(shadow,-54,11,109,26);
            using(var tire=new SolidBrush(C(26,37,43))){g.FillRectangle(tire,-48,-4,15,24);g.FillRectangle(tire,35,-4,15,24);}
            Polygon(g,C(198,46,42),new PointF(-48,11),new PointF(-44,-19),new PointF(-26,-39),new PointF(27,-39),new PointF(43,-19),new PointF(48,11));
            Polygon(g,C(137,208,214),new PointF(-31,-18),new PointF(-22,-33),new PointF(23,-33),new PointF(33,-18));
            using(var body=new SolidBrush(C(243,72,47)))g.FillRectangle(body,-44,-12,88,23);
            using(var dark=new SolidBrush(C(35,49,53))){g.FillRectangle(dark,-36,-6,72,5);g.FillRectangle(dark,-17,7,34,8);}
            using(var lights=new SolidBrush(C(255,204,132))){g.FillRectangle(lights,-36,2,19,5);g.FillRectangle(lights,18,2,19,5);}
            g.Restore(s);
        }
    }
}
