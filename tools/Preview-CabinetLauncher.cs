// Alternative concept only. The implemented launcher is the panoramic layout.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using Outrun.Setup;
class CabinetLauncherPreview {
 static void Text(Graphics g,string text,int x,int y,int size,Color c,bool bold=false){using(var f=new Font("Segoe UI",size,bold?FontStyle.Bold:FontStyle.Regular,GraphicsUnit.Pixel))using(var b=new SolidBrush(c))g.DrawString(text,f,b,x,y);}
 static void Rect(Graphics g,Color c,int x,int y,int w,int h){using(var b=new SolidBrush(c))g.FillRectangle(b,x,y,w,h);}
 static void Option(Graphics g,string text,int x,int y){using(var p=new Pen(CoastPalette.Muted))g.DrawRectangle(p,x,y+5,14,14);Text(g,text,x+25,y,15,CoastPalette.Ink);}
 [STAThread]static void Main(string[] args){
  using(var image=new Bitmap(960,670))using(var g=Graphics.FromImage(image))using(var art=new CoastScene{Size=new Size(900,222)})using(var scene=new Bitmap(900,222)){
   g.SmoothingMode=SmoothingMode.AntiAlias;g.TextRenderingHint=System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;g.Clear(CoastPalette.Ink);
   Rect(g,Color.FromArgb(7,30,40),0,0,960,32);Text(g,"OutRun Online Arcade",16,8,12,Color.FromArgb(188,214,221));Text(g,"—     □     ×",848,6,15,Color.FromArgb(188,214,221));
   art.DrawToBitmap(scene,new Rectangle(0,0,900,222));g.DrawImageUnscaled(scene,30,60);
   using(var p=new Pen(Color.FromArgb(243,231,188),2))g.DrawRectangle(p,29,59,902,224);
   Rect(g,CoastPalette.Red,30,40,115,5);Rect(g,CoastPalette.Gold,153,40,64,5);
   using(var b=new SolidBrush(CoastPalette.Paper))g.FillPolygon(b,new Point[]{new Point(30,306),new Point(930,306),new Point(889,559),new Point(71,559)});
   Text(g,"GAME PACKAGE",76,330,14,CoastPalette.Ink,true);Text(g,"PREFERENCES",592,330,14,CoastPalette.Ink,true);
   Rect(g,Color.White,76,366,468,146);
   using(var p=new Pen(Color.FromArgb(170,193,192))){p.DashPattern=new float[]{5,4};g.DrawRectangle(p,76,366,468,146);}
   Text(g,"Drop your game package here",98,386,19,CoastPalette.Ink,true);Text(g,"Original Xbox 360 package or supported folder.",98,423,14,CoastPalette.Muted);
   Rect(g,CoastPalette.Teal,344,457,176,37);Text(g,"Browse package",371,465,14,Color.White,true);
   Option(g,"Start in fullscreen",592,389);Option(g,"Create desktop shortcut",592,436);Text(g,"Both preferences are optional.",592,482,13,CoastPalette.Muted);
   Text(g,"Select your game package",54,588,19,Color.White,true);Text(g,"Your original files stay untouched.",54,621,14,Color.FromArgb(178,208,215));
   using(var b=new SolidBrush(Color.FromArgb(71,100,111)))g.FillPolygon(b,new Point[]{new Point(748,589),new Point(909,589),new Point(894,646),new Point(733,646)});
   Text(g,"Play",795,602,25,Color.FromArgb(158,184,190),true);
   image.Save(args[0],ImageFormat.Png);
  }
 }
}
