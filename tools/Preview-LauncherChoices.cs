// Design-selection previews only. Does not modify or launch the setup.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using Outrun.Setup;
class LauncherChoices {
 static Color Ink=Color.FromArgb(12,48,63),Cream=Color.FromArgb(248,246,232),Muted=Color.FromArgb(86,114,119),Red=Color.FromArgb(218,61,43),Gold=Color.FromArgb(255,184,44),Teal=Color.FromArgb(0,137,153);
 static void T(Graphics g,string t,int x,int y,int size,Color c,bool bold=false,bool italic=false){using(var f=new Font("Segoe UI",size,(bold?FontStyle.Bold:FontStyle.Regular)|(italic?FontStyle.Italic:FontStyle.Regular),GraphicsUnit.Pixel))using(var b=new SolidBrush(c))g.DrawString(t,f,b,x,y);}
 static void R(Graphics g,Color c,int x,int y,int w,int h){using(var b=new SolidBrush(c))g.FillRectangle(b,x,y,w,h);}
 static void P(Graphics g,Color c,params PointF[] pts){using(var b=new SolidBrush(c))g.FillPolygon(b,pts);}
 static void L(Graphics g,Color c,float x,float y,float x2,float y2,float width=1){using(var p=new Pen(c,width))g.DrawLine(p,x,y,x2,y2);}
 static void Box(Graphics g,int x,int y,int w,int h){R(g,Color.White,x,y,w,h);using(var p=new Pen(Color.FromArgb(169,192,192))){p.DashPattern=new float[]{5,4};g.DrawRectangle(p,x,y,w,h);}}
 static void Button(Graphics g,string t,int x,int y,int w,Color bg,Color fg){P(g,bg,new PointF(x+10,y),new PointF(x+w,y),new PointF(x+w-10,y+46),new PointF(x,y+46));T(g,t,x+(w-t.Length*8)/2,y+12,16,fg,true);}
 static void Option(Graphics g,string t,int x,int y){using(var p=new Pen(Muted))g.DrawRectangle(p,x,y+4,15,15);T(g,t,x+27,y,15,Ink);}
 static void TitleBar(Graphics g){R(g,Color.FromArgb(233,239,235),0,0,960,32);T(g,"OutRun Online Arcade",16,8,12,Ink);T(g,"—     □     ×",848,6,15,Ink);}
 static void Logo(Graphics g,int x,int y,int size){T(g,"OutRun",x,y,size,Ink,true,true);T(g,"O N L I N E   A R C A D E",x+4,y+size+4,12,Ink,true);}
 static void Palm(Graphics g,float x,float y,float scale){var s=g.Save();g.TranslateTransform(x,y);g.ScaleTransform(scale,scale);using(var p=new Pen(Color.FromArgb(21,94,86),7))g.DrawBezier(p,0,0,2,-42,23,-75,11,-105);for(int i=0;i<7;i++){double a=(-168+i*27)*Math.PI/180;float dx=(float)Math.Cos(a)*62,dy=(float)Math.Sin(a)*29;using(var p=new GraphicsPath()){p.AddBezier(11,-105,11+dx*.4f,-122+dy,11+dx,-113+dy,11+dx,-87+dy);p.AddBezier(11+dx,-87+dy,11+dx*.6f,-116+dy,19,-107,11,-105);using(var b=new SolidBrush(i%2==0?Color.FromArgb(7,116,98):Color.FromArgb(24,141,107)))g.FillPath(b,p);}}g.Restore(s);}
 [STAThread]static void Main(string[] args){Directory.CreateDirectory(args[0]);
  using(var img=new Bitmap(960,650))using(var g=Graphics.FromImage(img)){
   g.SmoothingMode=SmoothingMode.AntiAlias;g.TextRenderingHint=System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;g.Clear(Cream);TitleBar(g);
   R(g,Color.White,0,32,960,105);Logo(g,32,43,43);R(g,Red,31,123,97,4);R(g,Gold,128,123,97,4);R(g,Teal,225,123,97,4);
   using(var scene=new CoastScene{Size=new Size(960,236)})using(var art=new Bitmap(960,236)){scene.DrawToBitmap(art,new Rectangle(0,0,960,236));g.DrawImage(art,new Rectangle(770,46,155,75),new Rectangle(490,12,470,225),GraphicsUnit.Pixel);}
   // Instrument-like package status, not a fake performance meter.
   using(var b=new SolidBrush(Ink))g.FillEllipse(b,62,184,262,262);
   using(var p=new Pen(Color.FromArgb(54,87,97),9))g.DrawArc(p,80,202,226,226,135,270);
   using(var p=new Pen(Gold,9))g.DrawArc(p,80,202,226,226,135,38);
   for(int i=0;i<15;i++){double a=(135+i*270.0/14)*Math.PI/180;float x=(float)Math.Cos(a),y=(float)Math.Sin(a);L(g,Color.FromArgb(154,185,190),193+x*94,315+y*94,193+x*103,315+y*103,2);}
   T(g,"GAME FILES",141,261,14,Color.FromArgb(173,204,209),true);T(g,"WAITING",112,291,31,Color.White,true,true);T(g,"Choose a package",133,342,13,Color.FromArgb(173,204,209));
   T(g,"Game package",378,176,28,Ink,true);Box(g,378,225,548,204);
   T(g,"Drop a package or folder",400,248,22,Ink,true);T(g,"Use your original OutRun Xbox 360 package.",400,289,15,Muted);T(g,"Original files stay untouched.",400,318,14,Muted);Button(g,"Browse package",706,364,195,Gold,Ink);
   L(g,Color.FromArgb(206,214,201),34,466,926,466);T(g,"PREFERENCES",36,488,13,Muted,true);Option(g,"Start in fullscreen",244,485);Option(g,"Create desktop shortcut",530,485);
   R(g,Ink,0,553,960,97);T(g,"Select your package to continue",34,574,19,Color.White,true);T(g,"Play becomes available after import.",34,607,14,Color.FromArgb(177,206,210));Button(g,"Play",735,578,191,Color.FromArgb(68,99,109),Color.FromArgb(163,189,194));
   img.Save(Path.Combine(args[0],"launcher-choice-3-dashboard.png"),ImageFormat.Png);
  }
  using(var img=new Bitmap(960,650))using(var g=Graphics.FromImage(img)){
   g.SmoothingMode=SmoothingMode.AntiAlias;g.TextRenderingHint=System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;g.Clear(Cream);TitleBar(g);
   // Original poster composition, with a continuous coastal road as its spine.
   R(g,Color.FromArgb(44,185,210),0,32,364,618);
   using(var b=new SolidBrush(Color.FromArgb(255,220,100)))g.FillEllipse(b,201,80,123,123);
   P(g,Color.FromArgb(54,153,170),new PointF(0,275),new PointF(50,212),new PointF(104,257),new PointF(184,226),new PointF(232,274),new PointF(364,247),new PointF(364,350),new PointF(0,350));
   R(g,Teal,0,283,364,367);L(g,Color.FromArgb(96,209,209),0,289,364,289,4);
   for(int i=0;i<6;i++)L(g,Color.FromArgb(66,178,187),22,333+i*42,80+i*12,333+i*42);
   using(var p=new GraphicsPath()){p.AddBezier(364,280,230,321,380,347,221,422);p.AddBezier(221,422,98,482,125,548,20,650);p.AddLine(20,650,364,650);p.CloseFigure();using(var b=new SolidBrush(Color.FromArgb(244,222,178)))g.FillPath(b,p);}
   using(var p=new GraphicsPath()){p.AddBezier(356,284,235,324,397,352,255,430);p.AddBezier(255,430,153,487,196,551,114,650);p.AddLine(114,650,306,650);p.AddBezier(306,650,224,547,211,499,291,436);p.AddBezier(291,436,408,354,277,320,364,284);p.CloseFigure();using(var b=new SolidBrush(Color.FromArgb(47,75,84)))g.FillPath(b,p);using(var pen=new Pen(Cream,3))g.DrawPath(pen,p);}
   using(var pen=new Pen(Color.FromArgb(255,234,180),3)){pen.DashPattern=new float[]{6,7};g.DrawBezier(pen,360,286,252,325,403,358,275,433);g.DrawBezier(pen,275,433,171,498,240,555,207,650);}
   Palm(g,61,441,1.7f);Palm(g,311,337,.73f);
   var save=g.Save();g.TranslateTransform(225,537);g.RotateTransform(-16);R(g,Ink,-33,-3,12,27);R(g,Ink,24,-3,12,27);P(g,Red,new PointF(-34,17),new PointF(-31,-12),new PointF(-21,-32),new PointF(22,-32),new PointF(34,-12),new PointF(37,17));P(g,Color.FromArgb(164,222,220),new PointF(-24,-11),new PointF(-16,-26),new PointF(18,-26),new PointF(26,-11));R(g,Ink,-29,0,61,5);R(g,Gold,-29,9,14,4);R(g,Gold,18,9,14,4);g.Restore(save);
   R(g,Red,364,32,5,206);R(g,Gold,364,238,5,206);R(g,Ink,364,444,5,206);
   Logo(g,405,57,57);T(g,"Game package",411,196,25,Ink,true);Box(g,410,241,514,189);
   T(g,"Drop your package here",432,265,20,Ink,true);T(g,"Original Xbox 360 package or supported folder.",432,304,14,Muted);Button(g,"Browse package",431,357,197,Red,Color.White);
   T(g,"PREFERENCES",411,462,13,Muted,true);Option(g,"Start in fullscreen",411,496);Option(g,"Create desktop shortcut",656,496);
   L(g,Color.FromArgb(207,211,194),410,549,925,549);T(g,"Select a package to enable Play.",411,576,14,Muted);Button(g,"Play",752,575,173,Color.FromArgb(219,222,208),Color.FromArgb(112,130,126));T(g,"Your original files stay untouched.",411,605,12,Muted);
   img.Save(Path.Combine(args[0],"launcher-choice-4-postcard.png"),ImageFormat.Png);
  }
 }
}
