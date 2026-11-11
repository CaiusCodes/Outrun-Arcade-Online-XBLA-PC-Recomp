using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace Outrun.Setup {
    internal sealed class ImportForm:Form {
        readonly PortableGame game;
        readonly ArcadeButton browse=new ArcadeButton(),play=new ArcadeButton(),cancel=new ArcadeButton();
        readonly TextBox package=new TextBox();
        readonly Label status=new Label(),hint=new Label(),statusTitle=new Label(),packageNote=new Label();
        readonly LinkLabel details=new LinkLabel();
        readonly PackagePanel drop=new PackagePanel();
        readonly ToolTip tips=new ToolTip();
        readonly ImportProgress progress=new ImportProgress();
        readonly CheckBox fullscreen=new CheckBox(),desktop=new CheckBox();
        readonly ArcadeButton dataBrowse=new ArcadeButton(),dataDefault=new ArcadeButton();
        readonly PathLabel dataPath=new PathLabel();
        // Shortens long folders in the middle, so the folder's own name stays visible.
        sealed class PathLabel:Control {
            internal string Prefix="";
            internal PathLabel(){DoubleBuffered=true;SetStyle(ControlStyles.Selectable,false);}
            protected override void OnTextChanged(EventArgs e){Invalidate();base.OnTextChanged(e);}
            protected override void OnPaint(PaintEventArgs e){
                e.Graphics.Clear(BackColor);int x=0;
                if(Prefix.Length>0){using(var bold=new Font(Font,FontStyle.Bold)){TextRenderer.DrawText(e.Graphics,Prefix,bold,new Rectangle(0,0,Width,Height),CoastPalette.Teal,TextFormatFlags.VerticalCenter|TextFormatFlags.NoPadding|TextFormatFlags.SingleLine);x=TextRenderer.MeasureText(e.Graphics,Prefix,bold,Size,TextFormatFlags.NoPadding|TextFormatFlags.SingleLine).Width+10;}}
                TextRenderer.DrawText(e.Graphics,Text,Font,new Rectangle(x,0,Math.Max(0,Width-x),Height),ForeColor,TextFormatFlags.VerticalCenter|TextFormatFlags.PathEllipsis|TextFormatFlags.NoPadding|TextFormatFlags.SingleLine|TextFormatFlags.NoPrefix);
            }
        }
        CancellationTokenSource cancellation;bool busy,closeRequested,preview,ready,acceptReports;
        readonly string initial;
        string userData;
        internal ImportForm(PortableGame game,string input=null,bool preview=false) {
            this.game=game;initial=input;this.preview=preview;
            SuspendLayout();Text="OutRun Online Arcade";AutoScaleDimensions=new SizeF(96,96);AutoScaleMode=AutoScaleMode.Dpi;
            Font=new Font("Segoe UI",10);BackColor=CoastPalette.Paper;ForeColor=CoastPalette.Ink;
            ClientSize=new Size(960,740);FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;StartPosition=FormStartPosition.CenterScreen;
            Icon=Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            Controls.Add(new CoastScene{Location=Point.Empty,Size=new Size(960,236)});
            Controls.Add(new Label{Text="01   GAME PACKAGE",Location=new Point(28,258),Size=new Size(564,25),Font=new Font("Segoe UI",11,FontStyle.Bold)});
            Controls.Add(new Label{Text="02   PREFERENCES",Location=new Point(630,258),Size=new Size(302,25),Font=new Font("Segoe UI",11,FontStyle.Bold)});
            drop.Location=new Point(28,298);drop.Size=new Size(564,188);Controls.Add(drop);
            hint.Text="Add your game";hint.Location=new Point(22,18);hint.Size=new Size(518,31);hint.Font=new Font("Segoe UI",15,FontStyle.Bold);drop.Controls.Add(hint);
            packageNote.Text="Drop an original Xbox 360 package or supported folder.";packageNote.Location=new Point(22,56);packageNote.Size=new Size(518,28);packageNote.ForeColor=CoastPalette.Muted;drop.Controls.Add(packageNote);
            package.ReadOnly=true;package.BorderStyle=BorderStyle.None;package.BackColor=Color.White;package.Text="Your original package stays untouched.";package.Location=new Point(22,96);package.Size=new Size(518,24);package.AccessibleName="Selected game package";package.ForeColor=CoastPalette.Muted;package.Font=new Font("Segoe UI",9);drop.Controls.Add(package);
            ButtonStyle(browse,"Browse package",new Rectangle(342,132,198,40),false);drop.Controls.Add(browse);
            fullscreen.Text="Start in fullscreen";fullscreen.Location=new Point(630,310);fullscreen.Size=new Size(302,30);Controls.Add(fullscreen);
            Controls.Add(new Label{Text="Use the whole display when you play.",Location=new Point(651,343),Size=new Size(281,42),Font=new Font("Segoe UI",9),ForeColor=CoastPalette.Muted});
            desktop.Text="Create desktop shortcut";desktop.Location=new Point(630,400);desktop.Size=new Size(302,30);Controls.Add(desktop);
            Controls.Add(new Label{Text="Optional. You can also play from this folder.",Location=new Point(651,433),Size=new Size(281,46),Font=new Font("Segoe UI",9),ForeColor=CoastPalette.Muted});
            Controls.Add(new Label{Text="03   SAVE & USER DATA",Location=new Point(28,508),Size=new Size(904,25),Font=new Font("Segoe UI",11,FontStyle.Bold),UseMnemonic=false});
            var dataPanel=new Panel{Location=new Point(28,540),Size=new Size(904,56),BackColor=Color.White};
            dataPanel.Paint+=(s,e)=>{using(var pen=new Pen(Color.FromArgb(182,204,202)))e.Graphics.DrawRectangle(pen,0,0,dataPanel.Width-1,dataPanel.Height-1);};
            Controls.Add(dataPanel);
            dataPath.Location=new Point(16,17);dataPath.Size=new Size(560,24);dataPath.BackColor=Color.White;dataPath.Font=new Font("Segoe UI",9.5f);dataPath.AccessibleName="Save and user data folder";dataPanel.Controls.Add(dataPath);
            ButtonStyle(dataBrowse,"Browse folder",new Rectangle(592,9,170,38),false);dataBrowse.BackColor=Color.White;dataPanel.Controls.Add(dataBrowse);
            ButtonStyle(dataDefault,"Default",new Rectangle(770,9,118,38),false);dataDefault.BackColor=Color.White;dataPanel.Controls.Add(dataDefault);
            Controls.Add(new Label{Text="Default keeps saves in this game's resources\\userdata folder. Existing saves are copied when you change it, never moved.",Location=new Point(28,600),Size=new Size(904,20),Font=new Font("Segoe UI",9),ForeColor=CoastPalette.Muted,UseMnemonic=false});
            var footer=new Panel{Location=new Point(0,624),Size=new Size(960,116),BackColor=CoastPalette.Ink};Controls.Add(footer);
            statusTitle.Text="Select your game package";statusTitle.Location=new Point(28,19);statusTitle.Size=new Size(557,27);statusTitle.Font=new Font("Segoe UI",12,FontStyle.Bold);statusTitle.ForeColor=Color.White;footer.Controls.Add(statusTitle);
            status.Text="Play unlocks after the game files are imported.";status.Location=new Point(28,50);status.Size=new Size(557,39);status.Font=new Font("Segoe UI",9);status.ForeColor=Color.FromArgb(192,219,219);status.AutoEllipsis=true;footer.Controls.Add(status);
            details.Text="View details";details.Location=new Point(28,90);details.Size=new Size(200,23);details.Font=new Font("Segoe UI",9);details.LinkColor=Color.FromArgb(255,210,99);details.Visible=false;details.LinkClicked+=(s,e)=>MessageBox.Show(this,status.Text,"OutRun setup",MessageBoxButtons.OK,MessageBoxIcon.Information);footer.Controls.Add(details);
            progress.Location=new Point(0,0);progress.Size=new Size(960,5);progress.Visible=false;footer.Controls.Add(progress);
            ButtonStyle(cancel,"Cancel",new Rectangle(602,34,126,50),false);footer.Controls.Add(cancel);
            ButtonStyle(play,"Play",new Rectangle(756,29,176,60),true);play.Font=new Font("Segoe UI",16,FontStyle.Bold|FontStyle.Italic);footer.Controls.Add(play);
            play.Enabled=false;cancel.Enabled=false;cancel.Visible=false;AcceptButton=play;
            browse.Click+=async(s,e)=>{using(var dialog=new OpenFileDialog{Title="Choose OutRun Online Arcade's original Xbox 360 package",Filter="Xbox 360 package|*.*",RestoreDirectory=true})if(dialog.ShowDialog(this)==DialogResult.OK)await SelectInput(dialog.FileName);};
            cancel.Click+=(s,e)=>{if(cancellation!=null){cancel.Enabled=false;SetStatus("Cancelling import","Your original package and existing files are preserved.",false);cancellation.Cancel();}};
            dataBrowse.Click+=(s,e)=>{
                using(var dialog=new FolderBrowserDialog{Description="Choose where OutRun keeps saves and user data",ShowNewFolderButton=true}) {
                    try{if(Directory.Exists(userData))dialog.SelectedPath=userData;}catch(ArgumentException){}
                    if(dialog.ShowDialog(this)!=DialogResult.OK)return;
                    try{ShowUserData(PortableGame.CheckUserDataFolder(dialog.SelectedPath,game.Resources,game.DefaultUserData));}catch(Exception ex){SetStatus("Choose another save folder",ex.Message,true);}
                }
            };
            dataDefault.Click+=(s,e)=>ShowUserData(game.DefaultUserData);
            userData=game.UserData;ShowUserData(userData);
            play.Click+=(s,e)=>{try{game.SetUserData(userData);game.ApplyExtras(fullscreen.Checked,desktop.Checked);game.Launch();Close();}catch(Exception ex){SetStatus("Couldn't start the game",ex.Message,true);}};
            EnableDrop(this);
            FormClosing+=(s,e)=>{if(busy){closeRequested=true;if(cancellation!=null)cancellation.Cancel();e.Cancel=true;}};
            Shown+=async(s,e)=>{
                if(this.preview)return;
                busy=true;ApplyReady(false);SetStatus("Checking game files","Checking this portable folder.",false);
                bool checkFailed=false;
                try{ready=await Task.Run(()=>game.Ready());}catch(Exception ex){checkFailed=true;SetStatus("Couldn't check game files",ex.Message,true);}finally{busy=false;ApplyReady(ready);}
                if(closeRequested){Close();return;}if(ready)return;
                if(checkFailed)return;
                if(initial!=null){await SelectInput(initial);return;}
                try {
                    var old=game.NearbyLegacy();if(old.Count>0){string chosen=Choose(old,"Reuse an existing OutRun installation?");if(chosen!=null){await Run(()=>game.Migrate(chosen,Report,cancellation.Token));return;}}
                    await Find(game.Root,false);
                }catch(Exception ex){SetStatus("Couldn't find a package",ex.Message,true);}
            };
            ResumeLayout(false);PerformLayout();
        }
        static void ButtonStyle(ArcadeButton button,string text,Rectangle bounds,bool primary){button.Text=text;button.Bounds=bounds;button.Primary=primary;button.Font=new Font("Segoe UI",10,FontStyle.Bold);}
        protected override void Dispose(bool disposing){if(disposing)tips.Dispose();base.Dispose(disposing);}
        void SetStatus(string title,string message,bool error){statusTitle.Text=title;statusTitle.ForeColor=error?Color.FromArgb(255,210,99):Color.White;status.Text=message;status.AccessibleDescription=message;tips.SetToolTip(status,message);details.Visible=error;drop.HasValidPackage=play.Enabled&&!error;drop.Accent=error?CoastPalette.Red:play.Enabled?CoastPalette.Teal:Color.FromArgb(182,204,202);drop.Invalidate();}
        void ShowUserData(string path){userData=path;bool isDefault=game.IsDefaultUserData(path);dataPath.Prefix=isDefault?"DEFAULT":"CUSTOM";dataPath.Text=path;dataPath.ForeColor=CoastPalette.Ink;dataPath.AccessibleDescription=path;dataPath.Invalidate();tips.SetToolTip(dataPath,path);dataDefault.Enabled=!busy&&!isDefault;}
        void EnableDrop(Control control){control.AllowDrop=true;control.DragEnter+=(s,e)=>{if(!busy&&browse.Enabled&&e.Data.GetDataPresent(DataFormats.FileDrop)){e.Effect=DragDropEffects.Copy;drop.DragActive=true;drop.Invalidate();}};control.DragLeave+=(s,e)=>{drop.DragActive=false;drop.Invalidate();};control.DragDrop+=async(s,e)=>{drop.DragActive=false;drop.Invalidate();if(busy||!browse.Enabled||!e.Data.GetDataPresent(DataFormats.FileDrop))return;var paths=(string[])e.Data.GetData(DataFormats.FileDrop);if(paths.Length!=1){SetStatus("Choose one package","Drop one package or folder at a time.",true);return;}await SelectInput(paths[0]);};foreach(Control child in control.Controls)EnableDrop(child);}
        internal void RefreshReady(){ApplyReady(game.Ready());}
        void ApplyReady(bool isReady){ready=isReady;browse.Enabled=!busy&&!ready;play.Enabled=!busy&&ready;cancel.Visible=busy&&cancellation!=null;progress.Visible=busy;fullscreen.Enabled=desktop.Enabled=dataBrowse.Enabled=!busy;dataDefault.Enabled=!busy&&userData!=null&&!game.IsDefaultUserData(userData);if(ready){progress.Value=100;hint.Text="Game files ready";packageNote.Text="Imported and validated. No package needed next time.";if(package.Text=="Your original package stays untouched.")package.Text="Game files are stored in resources.";SetStatus("Ready to play","Choose your preferences, then click Play.",false);}}
        void Report(int value,string text){if(IsDisposed||!IsHandleCreated)return;BeginInvoke((Action)(()=>{if(!acceptReports||!busy||cancellation==null||cancellation.IsCancellationRequested)return;progress.Indeterminate=false;progress.Value=value;hint.Text=value>=95?"Finishing import":"Importing your game";SetStatus(value>=95?"Finishing import":"Importing game files  ·  "+value+"%",text,false);cancel.Enabled=value<95;}));}
        async Task Run(Action operation,string title="Checking game package") {
            if(busy)return;busy=true;acceptReports=true;cancellation=new CancellationTokenSource();ApplyReady(false);cancel.Enabled=true;progress.Value=0;progress.Indeterminate=true;hint.Text=title;SetStatus(title,"Please wait. You can cancel before the final step.",false);
            try{await Task.Run(operation);}catch(OperationCanceledException){acceptReports=false;hint.Text="Import cancelled";SetStatus("Import cancelled","Choose a package when you're ready. Your original files are untouched.",false);}catch(Exception ex){acceptReports=false;hint.Text="Package needs attention";SetStatus("Couldn't import this package",ex.Message,true);}
            acceptReports=false;bool installed=false;
            try{installed=await Task.Run(()=>game.Ready());}catch(Exception ex){SetStatus("Couldn't verify game files",ex.Message,true);}
            finally{busy=false;cancel.Enabled=false;cancellation.Dispose();cancellation=null;progress.Indeterminate=false;ApplyReady(installed);if(closeRequested)Close();}
        }
        async Task Find(string folder,bool nested) {
            List<string> choices=null;await Run(()=>{choices=PortableGame.FindPackages(folder,nested,cancellation.Token);},"Looking for a package");
            if(choices==null||closeRequested)return;if(choices.Count==0){hint.Text="Add your game";SetStatus("Select your game package","Play unlocks after the game files are imported.",false);return;}
            string chosen=choices.Count==1?choices[0]:Choose(choices,"Choose a compatible OutRun package");if(chosen!=null)await SelectInput(chosen);
        }
        async Task SelectInput(string path) {
            if(busy||ready)return;
            try {
                if(Directory.Exists(path)){string old=game.LegacyResources(path);if(old!=null){package.Text=old;await Run(()=>game.Migrate(old,Report,cancellation.Token));return;}await Find(path,true);return;}
                package.Text=path;await Run(()=>game.Import(path,Report,cancellation.Token));
            }catch(Exception ex){SetStatus("Couldn't open this package",ex.Message,true);}
        }
        string Choose(List<string> paths,string title) {
            using(var dialog=new Form{Text=title,ClientSize=new Size(700,300),StartPosition=FormStartPosition.CenterParent,MinimizeBox=false,MaximizeBox=false,Font=Font,AutoScaleMode=AutoScaleMode.Dpi}) {
                var list=new ListBox{Dock=DockStyle.Fill,HorizontalScrollbar=true};list.Items.AddRange(paths.ToArray());list.SelectedIndex=0;
                var buttons=new FlowLayoutPanel{Dock=DockStyle.Bottom,Height=48,FlowDirection=FlowDirection.RightToLeft};
                var okay=new Button{Text="Use selected",Width=140,Height=36,DialogResult=DialogResult.OK};var back=new Button{Text="Cancel",Width=100,Height=36,DialogResult=DialogResult.Cancel};buttons.Controls.Add(okay);buttons.Controls.Add(back);dialog.Controls.Add(list);dialog.Controls.Add(buttons);dialog.AcceptButton=okay;dialog.CancelButton=back;
                return dialog.ShowDialog(this)==DialogResult.OK?(string)list.SelectedItem:null;
            }
        }
        internal void Preview(string state) {
            if(state!="ready"&&state!="empty")package.Text="D:\\Xbox 360\\OutRun Online Arcade\\58410968";
            if(state=="selected"){hint.Text="Package selected";SetStatus("Package selected","Checking compatibility before importing game files.",false);}
            if(state=="validation"||state=="progress"){browse.Enabled=false;fullscreen.Enabled=desktop.Enabled=false;progress.Visible=true;progress.Value=state=="progress"?67:0;cancel.Enabled=true;cancel.Visible=true;hint.Text=state=="progress"?"Importing your game":"Checking game package";SetStatus(state=="progress"?"Importing game files  ·  67%":"Checking game package",state=="progress"?"Copying validated game files into this portable folder.":"Checking the package against this version of OutRun.",false);}
            if(state=="complete"){browse.Enabled=false;progress.Value=100;play.Enabled=true;hint.Text="Game files ready";packageNote.Text="Imported and validated. No package needed next time.";SetStatus("Ready to play","Choose your preferences, then click Play.",false);}
            if(state=="error"){hint.Text="Package needs attention";SetStatus("Incompatible game package","Choose OutRun Online Arcade version 0.0.1.5. Your original package has not been changed.",true);}
        }
        internal bool PlayEnabled {get{return play.Enabled;}}
        internal bool BrowseEnabled {get{return browse.Enabled;}}
    }
}
