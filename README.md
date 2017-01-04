# Screen_Record_Demo
### 这是一个linux环境下的基于QT的截屏及屏幕录像的测试Demo，大部分内容截取自SimpleScreenRecoder项目

1.基于QT，FFMPEG，Xlib 相关开源库

  其中Xlib用于获取屏幕原始图像，FFMPEG负责编码成帧，QT负责预览与测试，Xlib一般系统自带，FFMPEG与QT需自行安装
  
2.可截取任意大小屏幕区域或任意一个窗口

  这个Demo主要用于想在GUI应用中添加截屏和录像功能时，做一个简单的参考
  
3.SimpleScreenRecoder项目非常的棒，其功能完善，支持各种音视频输入，以及各种编码的配置
   
   SSR源码内容很多，音视频相关工作者可以参考，这里只截取了一点视频编码相关的内容
   
4.内容相对粗糙简单，测试功能流程之用，不包含音频部分，不合理的地方请忽略
   
   ---屏幕录像测试视频见 test.mkv---
