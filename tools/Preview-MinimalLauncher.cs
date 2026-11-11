// Design concepts only. Reuses the repository-owned procedural CoastalArt.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using Outrun.Setup;
class MinimalLauncherPreview {
 static Color Ink=Color.FromArgb(17,50,63), Muted=Color.FromArgb(98,120,129);
 static void Box(Graphics g,Color c,int x,int y,int w,int h,int r=12) {
  using(var p=new GraphicsPath())using(var b=new SolidBrush(c)) {
   p.AddArc(x,y,r*2,r*2,180,90);p.AddArc(x+w-r*2,y,r*2,r*2,270,90);
   p.AddArc(x+w-r*2,y+h-r*2,r*2,r*2,0,90);p.AddArc(x,y+h-r*2,r*2,r*2,90,90);p.CloseFigure();g.FillPath(b,p);
  }
 }
 static void Text(Graphics g,string t,int x,int y,int size,Color c,bool bold=false,bool italic=false) {
  using(var f=new Font("Segoe UI",size, (bold?FontStyle.Bold:FontStyle.Regular)|(italic?FontStyle.Italic:FontStyle.Regular),GraphicsUnit.Pixel))using(var b=new SolidBrush(c))g.DrawString(t,f,b,x,y);
 }
 static void Option(Graphics g,string t,int x,int y,Color fg,Color edge) {
  using(var p=new Pen(edge,1.2f))g.DrawRectangle(p,x,y+5,14,14);Text(g,t,x+27,y,15,fg);
 }
 [STAThread] static void Main(string[] args) {
  Directory.CreateDirectory(args[0]);
  using(var art=new CoastalArt{Size=new Size(300,620)})using(var illustration=new Bitmap(300,620)) {
   art.DrawToBitmap(illustration,new Rectangle(0,0,300,620));
   for(int theme=0;theme<3;theme++)using(var image=new Bitmap(860,560))using(var g=Graphics.FromImage(image)) {
    g.SmoothingMode=SmoothingMode.AntiAlias;g.TextRenderingHint=System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
    bool dark=theme==1;var bg=dark?Color.FromArgb(12,31,43):Color.FromArgb(246,249,249);
    var fg=dark?Color.FromArgb(233,245,247):Ink;var muted=dark?Color.FromArgb(146,176,185):Muted;
    g.Clear(bg);
    Text(g,"OutRun Online Arcade",22,12,14,muted);
    Text(g,"—     □     ×",742,12,14,muted);
    if(theme==0) {
     using(var b=new SolidBrush(Color.FromArgb(207,243,239)))g.FillRectangle(b,0,42,860,122);
     Text(g,"OutRun",38,52,42,Ink,true,true);Text(g,"ONLINE ARCADE",41,108,12,Ink,true);
     g.DrawImage(illustration,new Rectangle(580,42,280,122),new Rectangle(0,194,300,155),GraphicsUnit.Pixel);
    } else if(theme==1) {
     Text(g,"OutRun",42,60,44,fg,true,true);Text(g,"ONLINE ARCADE",45,118,12,Color.FromArgb(68,208,211),true);
     using(var p=new Pen(Color.FromArgb(68,208,211),2))g.DrawLine(p,43,156,815,156);
     g.DrawImage(illustration,new Rectangle(674,52,140,91),new Rectangle(0,240,300,200),GraphicsUnit.Pixel);
    } else {
     Text(g,"OutRun",42,56,38,Ink,true,true);Text(g,"ONLINE ARCADE",44,105,12,Muted,true);
     g.DrawImage(illustration,new Rectangle(630,42,230,518),new Rectangle(0,177,300,443),GraphicsUnit.Pixel);
    }
    int x=42, y=theme==2?155:193, width=theme==2?544:774;
    Text(g,"Game package",x,y,26,fg,true);
    Text(g,"Choose your original Xbox 360 package.",x,y+42,15,muted);
    var surface=dark?Color.FromArgb(21,49,62):Color.White;
    Box(g,surface,x,y+79,width,104);
    using(var p=new Pen(dark?Color.FromArgb(53,93,107):Color.FromArgb(211,227,230)))g.DrawRectangle(p,x+1,y+80,width-2,102);
    Text(g,"Drop a package or folder here",x+20,y+98,16,fg,true);
    Text(g,"Your original package stays untouched.",x+20,y+132,13,muted);
    Box(g,dark?Color.FromArgb(65,202,209):Color.FromArgb(14,125,145),x+width-174,y+105,154,42,8);
    Text(g,"Browse package",x+width-163,y+115,15,dark?Ink:Color.White,true);
    Text(g,"Extras",x,y+208,14,muted,true);
    Option(g,"Start in fullscreen",x,y+240,fg,muted);
    Option(g,"Create desktop shortcut",x+(theme==2?0:264),y+(theme==2?275:240),fg,muted);
    int bottom=507;
    Text(g,"Select a package to enable Play.",x,bottom+9,13,muted);
    Box(g,dark?Color.FromArgb(41,65,77):Color.FromArgb(220,231,233),x+width-136,bottom,136,40,8);
    Text(g,"Play",x+width-86,bottom+9,15,muted,true);
    image.Save(Path.Combine(args[0],"launcher-concept-"+(char)('A'+theme)+".png"),ImageFormat.Png);
   }
  }
 }
}
