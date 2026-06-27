type UiColor = `#${string}`;

type UiLabel = {
  text: string;
  x: number;
  y: number;
  color: UiColor;
};

type UiButton = {
  id: string;
  className?: string;
  text: string;
  subtitle?: string;
  x: number;
  y: number;
  w: number;
  h: number;
  bg: UiColor;
  fg: UiColor;
  accent?: UiColor;
  radius?: number;
  target: string;
};

type UiScreen = {
  id: string;
  className?: string;
  background: UiColor;
  labels: UiLabel[];
  buttons: UiButton[];
};

type UiApp = {
  initialScreen: string;
  screens: UiScreen[];
};

export default {
  initialScreen: "home",
  screens: [
    {
      id: "home",
      className: "page home-page",
      background: "#f5f8fb",
      labels: [
        {
          text: "SMART PANEL",
          x: 54,
          y: 30,
          color: "#2874ff",
        },
        {
          text: "开发板触摸控制台",
          x: 54,
          y: 72,
          color: "#172033",
        },
      ],
      buttons: [
        {
          id: "open_album",
          className: "glass-card album-card",
          text: "01  电子相册",
          subtitle: "左右滑动切换图片",
          x: 54,
          y: 144,
          w: 692,
          h: 88,
          bg: "#ffffff",
          fg: "#172033",
          accent: "#2874ff",
          radius: 28,
          target: "album",
        },
        {
          id: "open_monitor",
          className: "glass-card monitor-card",
          text: "02  数据监测",
          subtitle: "动态曲线 / 阈值 / 报警预留",
          x: 54,
          y: 252,
          w: 692,
          h: 88,
          bg: "#ffffff",
          fg: "#172033",
          accent: "#00a7b5",
          radius: 28,
          target: "monitor",
        },
        {
          id: "open_device",
          className: "glass-card device-card",
          text: "03  LED灯及蜂鸣器控制",
          subtitle: "触摸控制 D7-D10 与蜂鸣器",
          x: 54,
          y: 360,
          w: 692,
          h: 88,
          bg: "#ffffff",
          fg: "#172033",
          accent: "#ff7a45",
          radius: 28,
          target: "device",
        },
      ],
    },
    {
      id: "monitor",
      className: "page monitor-page",
      background: "#f5fbff",
      labels: [
        {
          text: "DATA MONITOR",
          x: 54,
          y: 30,
          color: "#00a7b5",
        },
        {
          text: "数据监测",
          x: 54,
          y: 72,
          color: "#172033",
        },
        {
          text: "阈值、曲线和报警区域已预留",
          x: 215,
          y: 386,
          color: "#708092",
        },
      ],
      buttons: [
        {
          id: "monitor_back",
          className: "glass-pill monitor-back",
          text: "返回首页",
          x: 612,
          y: 36,
          w: 132,
          h: 54,
          bg: "#ffffff",
          fg: "#172033",
          accent: "#00a7b5",
          radius: 24,
          target: "home",
        },
      ],
    },
    {
      id: "device",
      className: "page device-page",
      background: "#fff8f4",
      labels: [
        {
          text: "DEVICE PANEL",
          x: 54,
          y: 30,
          color: "#ff7a45",
        },
        {
          text: "LED灯及蜂鸣器控制",
          x: 54,
          y: 72,
          color: "#172033",
        },
        {
          text: "触摸开关已接入 LED / 蜂鸣器",
          x: 138,
          y: 386,
          color: "#708092",
        },
      ],
      buttons: [
        {
          id: "device_back",
          className: "glass-pill device-back",
          text: "返回首页",
          x: 612,
          y: 36,
          w: 132,
          h: 54,
          bg: "#ffffff",
          fg: "#172033",
          accent: "#ff7a45",
          radius: 24,
          target: "home",
        },
      ],
    },
  ],
} satisfies UiApp;
