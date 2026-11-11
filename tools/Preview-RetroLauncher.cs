// Selection mockups only; no launcher/runtime/import changes.
// All illustration crops reuse the repository-owned procedural CoastalArt.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using Outrun.Setup;
class RetroLauncherPreview {
 static Color C(int r,int g,int b){return Color.FromArgb(r,g,b);}
 static void Fill(Graphics g,Color c,int x,int y,int w,int h){using(var b=new SolidBrush(c))g.FillRectangle(b,x,y,w,h);}
 static void Text(Graphics g,string text,int x,int y,int size,Color c,bool bold=false,bool italic=false){
  using(var f=new Font("Segoe UI",size,(bold?FontStyle.Bold:FontStyle.Regular)|(italic?FontStyle.Italic:FontStyle.Regular),GraphicsUnit.Pixel))
  using(var b=new SolidBrush(c))g.DrawString(text,f,b,x,y);
 }
 static void Stroke(Graphics g,Color c,int x,int y,int w,int h,bool dashed=false){using(var p=new Pen(c,1)){if(dashed)p.DashPattern=new float[]{5,4};g.DrawRectangle(p,x,y,w,h);}}
 static void Option(Graphics g,string text,int x,int y,Color ink,Color edge){Stroke(g,edge,x,y+4,15,15);Text(g,text,x+28,y,15,ink);}
 static void Slashes(Graphics g,Color c,int x,int y,int height){using(var b=new SolidBrush(c))for(int i=0;i<3;i++)g.FillPolygon(b,new Point[]{new Point(x+i*19,y+height),new Point(x+height+i*19,y),new Point(x+height+10+i*19,y),new Point(x+10+i*19,y+height)});}
 static void Button(Graphics g,string text,int x,int y,int w,Color bg,Color fg,bool slant){
  using(var b=new SolidBrush(bg)){
   if(slant)g.FillPolygon(b,new Point[]{new Point(x+12,y),new Point(x+w,y),new Point(x+w-12,y+44),new Point(x,y+44)});
   else g.FillRectangle(b,x,y,w,44);
  }
  using(var f=new Font("Segoe UI",15,FontStyle.Bold,GraphicsUnit.Pixel))using(var b=new SolidBrush(fg))using(var format=new StringFormat{Alignment=StringAlignment.Center,LineAlignment=StringAlignment.Center})g.DrawString(text,f,b,new RectangleF(x,y,w,44),format);
 }
 static void Logo(Graphics g,int x,int y,Color ink,Color sub,int size){Text(g,"OutRun",x,y,size,ink,true,true);Text(g,"O N L I N E   A R C A D E",x+3,y+size+3,12,sub,true);}
 static void TitleBar(Graphics g,Color bg,Color ink){Fill(g,bg,0,0,920,32);Text(g,"OutRun Online Arcade",15,8,12,ink);Text(g,"—     □     ×",811,6,15,ink);}
 static void Crop(Graphics g,Bitmap art,Rectangle target,Rectangle source,float warm=0){
  if(warm==0){g.DrawImage(art,target,source,GraphicsUnit.Pixel);return;}
  using(var attrs=new ImageAttributes()){
   var matrix=warm>0?new ColorMatrix(new float[][]{new float[]{.70f,0,0,0,0},new float[]{.45f,.70f,0,0,0},new float[]{.18f,.05f,.60f,0,0},new float[]{0,0,0,1,0},new float[]{.16f,.05f,.01f,0,1}}):new ColorMatrix(new float[][]{new float[]{.30f,0,0,0,0},new float[]{0,.50f,0,0,0},new float[]{0,0,.72f,0,0},new float[]{0,0,0,1,0},new float[]{0,.03f,.09f,0,1}});
   attrs.SetColorMatrix(matrix);g.DrawImage(art,target,source.X,source.Y,source.Width,source.Height,GraphicsUnit.Pixel,attrs);
  }
 }
 [STAThread]static void Main(string[] args){
  Directory.CreateDirectory(args[0]);
  using(var art=new CoastalArt{Size=new Size(600,1240)})using(var illustration=new Bitmap(600,1240)){
   art.DrawToBitmap(illustration,new Rectangle(0,0,600,1240));
   for(int theme=0;theme<3;theme++)using(var image=new Bitmap(920,620))using(var g=Graphics.FromImage(image)){
    g.SmoothingMode=SmoothingMode.AntiAlias;g.TextRenderingHint=System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
    Color ink=C(15,44,65),muted=C(93,115,127),edge=C(190,214,224),accent=C(255,170,25),paper=C(244,249,252);
    if(theme==0){
     g.Clear(paper);TitleBar(g,C(232,243,249),ink);
     using(var grad=new LinearGradientBrush(new Rectangle(0,32,920,146),C(36,195,230),C(16,118,208),90))g.FillRectangle(grad,0,32,920,146);
     Crop(g,illustration,new Rectangle(608,32,312,146),new Rectangle(0,354,600,282));
     Fill(g,C(5,43,79),36,65,295,67);Logo(g,51,65,Color.White,Color.White,39);
     Slashes(g,accent,330,65,67);Fill(g,accent,0,174,920,4);
     Text(g,"Game package",42,209,27,ink,true,true);
     Text(g,"Choose your original OutRun Xbox 360 package.",42,254,15,muted);
     Fill(g,Color.White,42,293,836,128);Stroke(g,edge,42,293,835,127);
     Text(g,"Drop a package or folder here",63,321,17,ink,true);
     Text(g,"Your original package stays untouched.",63,359,14,muted);
     Button(g,"Browse package",669,330,186,accent,ink,true);
     Text(g,"Extras (optional)",42,457,14,muted,true);
     Option(g,"Start in fullscreen",42,490,ink,muted);Option(g,"Create desktop shortcut",310,490,ink,muted);
     Fill(g,C(225,236,242),0,544,920,76);Slashes(g,C(198,216,227),39,569,20);
     Button(g,"Play",731,560,147,C(203,219,228),C(108,128,140),true);
    }else if(theme==1){
     paper=C(250,246,235);ink=C(43,62,66);muted=C(112,116,108);edge=C(218,210,189);accent=C(213,69,50);
     g.Clear(paper);TitleBar(g,C(239,233,219),ink);
     Crop(g,illustration,new Rectangle(0,32,263,588),new Rectangle(0,351,600,889),1);
     Fill(g,paper,18,50,229,102);Logo(g,31,60,ink,accent,43);
     Fill(g,accent,263,32,4,588);
     Text(g,"Game package",302,86,27,ink,true);
     Text(g,"Choose your original OutRun",302,132,15,muted);Text(g,"Xbox 360 package.",302,155,15,muted);
     Fill(g,C(255,253,247),302,215,576,193);Stroke(g,edge,302,215,575,192,true);
     Text(g,"Drop a package or folder here",327,240,17,ink,true);
     Text(g,"Your original package stays untouched.",327,275,14,muted);
     Button(g,"Browse package",326,327,187,accent,Color.White,false);
     Text(g,"Extras (optional)",302,445,14,muted,true);
     Option(g,"Start in fullscreen",302,478,ink,muted);Option(g,"Create desktop shortcut",566,478,ink,muted);
     Fill(g,edge,302,539,576,1);Button(g,"Play",731,560,147,C(229,226,214),C(135,138,126),false);
    }else{
     ink=C(235,247,248);muted=C(156,186,197);edge=C(54,88,106);accent=C(57,207,212);paper=C(13,32,46);
     g.Clear(paper);TitleBar(g,C(8,24,36),muted);
     using(var grad=new LinearGradientBrush(new Rectangle(0,32,920,158),C(13,44,64),C(24,76,102),0f))g.FillRectangle(grad,0,32,920,158);
     Crop(g,illustration,new Rectangle(584,32,336,158),new Rectangle(0,354,600,282),-1);
     Logo(g,43,66,ink,accent,46);
     Fill(g,accent,0,188,920,2);
     Text(g,"Game package",42,222,27,ink,true);
     Text(g,"Choose your original OutRun Xbox 360 package.",42,268,15,muted);
     Fill(g,C(19,45,61),42,309,836,126);Stroke(g,edge,42,309,835,125);
     Text(g,"Drop a package or folder here",65,337,17,ink,true);
     Text(g,"Your original package stays untouched.",65,376,14,muted);
     Button(g,"Browse package",667,350,188,accent,C(8,41,51),false);
     Text(g,"Extras (optional)",42,469,14,muted,true);
     Option(g,"Start in fullscreen",42,504,ink,muted);Option(g,"Create desktop shortcut",310,504,ink,muted);
     Fill(g,edge,42,550,836,1);Button(g,"Play",731,566,147,C(32,58,74),C(111,141,157),false);
    }
    image.Save(Path.Combine(args[0],"launcher-retro-"+(char)('D'+theme)+".png"),ImageFormat.Png);
   }
  }
 }
}
