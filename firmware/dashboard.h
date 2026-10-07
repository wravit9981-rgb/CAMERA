/*
 * dashboard.h — Smart AirGuard 구역별 관제 화면 (claude.ai 아티팩트 "Smart AirGuard 구역별 관제 (최종본)" 기반)
 *   ESP32 가 "/" 로 보내는 HTML 한 장. 데이터는 1초마다 "/data" (마스터 UART JSON) 를 읽음
 *   아티팩트에서 바꾼 점: 외부 폰트 제거(인터넷 없는 AP 대비), 항상 실데이터 모드,
 *                       끼임(고장코드 5) 표시, CO2 칸 = 두 구역 eCO2 평균
 *   ?d=1~7 시안 선택, ?demo=1 데모 재생
 */
#pragma once
#include <pgmspace.h>

const char INDEX_HTML[] PROGMEM = R"AGHTML(<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Smart AirGuard 구역별 관제</title>
<script>window.AG_LIVE = 1;</script>
<style>
/* ---------- 미리보기 페이지 테두리(툴바) : 태블릿 실전 화면(kiosk)에서는 숨김 ---------- */
:root{ --pg:#C3CBD4; --tb:#F4F6F8; --tx:#1E2833; --mu:#5A6673; --bt:#E2E7EC; --on:#1E2833; --ontx:#FFFFFF; --ln:#D3DAE1;
  box-sizing:border-box; padding-top:env(safe-area-inset-top,0px); padding-bottom:env(safe-area-inset-bottom,0px); }
@media (prefers-color-scheme: dark){ :root:not([data-theme="light"]){ --pg:#14181C; --tb:#20262C; --tx:#E8ECEF; --mu:#9AA5B1; --bt:#2E363E; --on:#E8ECEF; --ontx:#14181C; --ln:#343D46; } }
:root[data-theme="dark"]{ --pg:#14181C; --tb:#20262C; --tx:#E8ECEF; --mu:#9AA5B1; --bt:#2E363E; --on:#E8ECEF; --ontx:#14181C; --ln:#343D46; }
html{ height:100%; scroll-padding-top:env(safe-area-inset-top,0px); }
html,body{ margin:0; padding:0; overflow:hidden; }
body{ height:100%; background:#C3CBD4; background:var(--pg); font-family:"Noto Sans KR","Noto Sans CJK KR","Apple SD Gothic Neo","Malgun Gothic",Roboto,sans-serif; -webkit-text-size-adjust:100%; }
#tb{ position:absolute; left:0; right:0; top:0; padding:8px 12px; background:#F4F6F8; background:var(--tb); color:#1E2833; color:var(--tx);
  border-bottom:1px solid var(--ln); font-size:13px; display:-webkit-flex; display:flex; -webkit-flex-wrap:wrap; flex-wrap:wrap; -webkit-align-items:center; align-items:center; gap:6px 14px; z-index:5; box-sizing:border-box; }
#tb .ti{ font-weight:900; font-size:14px; margin-right:4px; white-space:nowrap; }
#tb .g{ display:-webkit-flex; display:flex; -webkit-align-items:center; align-items:center; gap:4px; white-space:nowrap; }
#tb .gl{ color:var(--mu); font-weight:700; margin-right:2px; }
#tb button,#tb select{ font:inherit; font-weight:700; border:0; border-radius:8px; padding:6px 10px; background:var(--bt); color:var(--tx); cursor:pointer; }
#tb button.on{ background:var(--on); color:var(--ontx); }
#tb button:focus-visible,#tb select:focus-visible{ outline:2px solid #00C29B; outline-offset:2px; }
#tb .hint{ color:var(--mu); margin-left:auto; white-space:nowrap; }
#fit{ position:absolute; left:0; right:0; bottom:0; top:52px; }
body.kiosk #tb{ display:none; } body.kiosk #fit{ top:0; } body.kiosk #stage{ box-shadow:none; }

/* ---------- 무대 (1422 x 800, 화면에 맞춰 통째로 확대/축소) ----------
   태블릿 구형 브라우저 호환: 무대 안쪽은 CSS 변수·Grid 미사용, 절대 배치 위주 */
#stage{ position:absolute; left:0; top:0; width:1422px; height:800px; overflow:hidden; -webkit-transform-origin:0 0; transform-origin:0 0;
  color:#1E2833; box-shadow:0 18px 50px rgba(0,0,0,.28); word-break:keep-all; font-family:"Noto Sans KR","Noto Sans CJK KR","Apple SD Gothic Neo","Malgun Gothic",Roboto,sans-serif; }
#stage *{ -webkit-box-sizing:border-box; box-sizing:border-box; }
#stage svg{ overflow:visible; }
.num{ font-weight:700; letter-spacing:-.03em; line-height:1; font-variant-numeric:tabular-nums; white-space:nowrap; }
.u{ font-size:20px; font-weight:500; color:#5A6673; margin-left:5px; letter-spacing:0; }
.nid{ font-family:"Roboto Mono",Consolas,"DejaVu Sans Mono",monospace; font-size:14px; font-weight:700; color:#5A6673; letter-spacing:0; }
.chip{ display:inline-block; padding:2px 12px 2px 10px; border-radius:14px; font-size:15px; font-weight:700; line-height:22px; white-space:nowrap; }
.chip i{ display:inline-block; width:9px; height:9px; border-radius:5px; margin-right:6px; vertical-align:1px; }
.chip.ok{ background:#D5F5EA; color:#007A62; } .chip.ok i{ background:#00C29B; }
.chip.bad{ background:#FBE0E0; color:#B0141B; } .chip.bad i{ background:#D21018; }
.chip.off{ background:#E6EAEE; color:#5A6673; } .chip.off i{ background:#8E98A3; }
.bar{ position:relative; height:10px; border-radius:5px; background:rgba(30,40,51,.09); overflow:hidden; }
.bar b{ position:absolute; left:0; top:0; bottom:0; width:0; border-radius:5px; -webkit-transition:width .6s ease, background-color .4s; transition:width .6s ease, background-color .4s; }
.bar s{ position:absolute; top:0; bottom:0; width:2px; margin-left:-1px; background:rgba(255,255,255,.9); }
.ico svg{ display:block; width:100%; height:100%; }
.c2{ display:inline-block; padding:4px 14px; border-radius:16px; font-size:17px; font-weight:700; line-height:22px; }
.c2.lv0{ background:#D5F5EA; color:#007A62; } .c2.lv1{ background:#FFF1CC; color:#8A5E00; } .c2.lv2{ background:#FFE2D1; color:#A33E00; } .c2.lv3{ background:#FBE0E0; color:#B0141B; }
.ev{ position:relative; padding:6px 0 6px 18px; font-size:15px; line-height:20px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; border-bottom:1px solid rgba(30,40,51,.08); }
.ev i{ position:absolute; left:0; top:11px; width:10px; height:10px; border-radius:5px; }
.ev .t{ color:#5A6673; margin-right:8px; font-variant-numeric:tabular-nums; }

/* 공통 헤더 */
.hdr{ position:absolute; left:24px; top:14px; width:1374px; height:64px; }
.hdr .clk{ position:absolute; left:0; top:0; font-size:52px; font-weight:900; line-height:64px; letter-spacing:-1.5px; font-variant-numeric:tabular-nums; }
.hdr .hdt{ position:absolute; left:156px; top:6px; height:52px; }
.hdr .wifi{ position:absolute; left:0; top:8px; width:46px; height:34px; }
.hdr .wifi g{ stroke:#46525E; } .hdr .wifi circle{ fill:#46525E; }
.hdr .wifi.lv3 g{ stroke:#B9C1C9; } .hdr .wifi.lv3 circle{ fill:#B9C1C9; }
.hdr .dt{ position:absolute; left:58px; top:0; font-size:18px; font-weight:700; line-height:26px; white-space:nowrap; color:#46525E; }
.hdr .brand{ position:absolute; left:58px; top:26px; font-size:19px; font-weight:900; line-height:26px; white-space:nowrap; }
.hdr .brand b.lv0{ color:#00A884; } .hdr .brand b.lv3{ color:#D21018; }
.hdr .pill{ position:absolute; left:524px; top:6px; width:376px; height:52px; border-radius:26px; color:#FFF; text-align:center; line-height:52px; font-size:26px; font-weight:900;
  -webkit-transition:background-color .4s; transition:background-color .4s; box-shadow:0 8px 20px rgba(55,65,110,.2); white-space:nowrap; }
.hdr .pill span{ font-weight:500; font-size:20px; margin-right:14px; opacity:.92; }
.hdr .pill em{ font-style:normal; font-weight:700; font-size:19px; margin-left:8px; opacity:.92; }
.pill.lv0{ background:#00C29B; } .pill.lv1{ background:#F4AC00; } .pill.lv2{ background:#E55A00; } .pill.lv3{ background:#D21018; }
.hdr .mn{ position:absolute; left:920px; top:6px; width:226px; height:52px; border-radius:16px; background:rgba(255,255,255,.62); padding:4px 14px; }
.hdr .mn .mnt{ font-size:15px; font-weight:900; line-height:20px; }
.hdr .mn .chip{ font-size:14px; line-height:20px; padding:1px 10px 1px 8px; }
.hdr .mn .nid{ position:absolute; right:14px; bottom:7px; }
.hdr .conn{ position:absolute; right:0; top:8px; text-align:right; font-size:16px; font-weight:700; line-height:24px; white-space:nowrap; }
.hdr .conn .cd{ display:inline-block; width:10px; height:10px; border-radius:5px; margin-right:6px; vertical-align:1px; }
.hdr .conn .cd.lv0{ background:#00C29B; } .hdr .conn .cd.lv3{ background:#D21018; }
.hdr .conn .upd{ color:#5A6673; font-weight:500; font-variant-numeric:tabular-nums; }
.hdr .demo{ display:none; margin-left:8px; padding:0 8px; border:1px solid #AEB7C0; border-radius:10px; font-size:12px; color:#5A6673; line-height:18px; font-weight:500; }
body.demo .hdr .demo{ display:inline-block; }
/* =====================================================================
   E1 / E2 : 최종안 D 라이트 테마 + 실시간 미니 그래프 + 상단 종합 배너
   ===================================================================== */
#stage.ee{ background:#B2B9C0; color:#1F2933; }
.ee .num{ font-weight:700; color:#1F2933; letter-spacing:-.03em; }
.ee .u{ font-size:20px; font-weight:700; color:#3E4852; }
.ee .nid{ color:#4B5560; }
.ee .chip{ background:none; padding:0; font-size:15px; font-weight:800; color:#2E3842; }
.ee .chip i{ width:10px; height:10px; border-radius:2px; background:#5F6A75; }
.ee .chip.bad{ color:#D21018; } .ee .chip.bad i{ background:#D21018; }
.ee svg path,.ee svg circle,.ee svg line{ vector-effect:none; }
.ee .bi i{ display:inline-block; }
.ee .bi svg,.ee .zi svg,.ee .eck svg{ fill:none; stroke:currentColor; stroke-width:2; stroke-linecap:round; stroke-linejoin:round; }
.ee .lc.lv0 .s1,.ee .ebn.lv0 .s1,.ee .zst.lv0 .s1{ display:none; }
.ee .lc.lv1 .s0,.ee .lc.lv2 .s0,.ee .lc.lv3 .s0,.ee .ebn.lv1 .s0,.ee .ebn.lv2 .s0,.ee .ebn.lv3 .s0,.ee .zst.lv1 .s0,.ee .zst.lv2 .s0,.ee .zst.lv3 .s0{ display:none; }
.ee .ic{ display:inline-block; width:34px; height:34px; border-radius:7px; padding:3px; margin-right:8px; vertical-align:top; background:#EAECEE; border:1.5px solid #5F6A75; color:#3E4852; }
.ee .ic svg{ display:block; width:100%; height:100%; }
.ee .lab,.ee .el{ font-size:18px; font-weight:800; line-height:36px; white-space:nowrap; color:#1F2933; }
.ee .lab small,.ee .el small{ font-size:15px; font-weight:600; color:#4B5560; margin-left:6px; }
.ee .c2{ border-radius:6px; }

/* ---------- 헤더 ---------- */
.eh{ position:absolute; left:24px; top:10px; width:1374px; height:58px; }
.eh .elogo{ position:absolute; left:0; top:4px; height:50px; line-height:50px; color:#1F2933; }
.eh .elogo svg{ float:left; width:42px; height:42px; margin:4px 12px 0 0; color:#2E3842; }
.eh .elogo b{ font-size:26px; font-weight:900; letter-spacing:-.5px; }
.eh .est{ position:absolute; left:300px; top:9px; height:40px; line-height:40px; padding:0 16px 0 8px; border-radius:20px; background:#EAECEE; border:1px solid #9AA3AC; font-size:18px; font-weight:800; white-space:nowrap; }
.eh .eck{ display:inline-block; width:28px; height:28px; vertical-align:-8px; margin-right:8px; }
.eh .eck svg{ width:28px; height:28px; stroke-width:2.2; }
.eh .eck.lv0{ color:#1E8A4A; } .eh .eck.lv3{ color:#D21018; }
.eh .est .lv0{ color:#1F2933; }
.eh .demo{ display:none; margin-left:10px; padding:0 8px; border:1px solid #9AA3AC; border-radius:10px; font-size:12px; color:#57606B; line-height:18px; font-weight:600; vertical-align:2px; }
body.demo .eh .demo{ display:inline-block; }
.eh .emn{ position:absolute; left:720px; top:9px; height:40px; line-height:40px; padding:0 14px; border-radius:8px; background:#EAECEE; border:1px solid #9AA3AC; white-space:nowrap; }
.eh .emn .k{ font-size:16px; font-weight:900; margin-right:10px; } .eh .emn .nid{ margin-left:10px; }
.eh .etm{ position:absolute; right:190px; top:6px; text-align:right; font-size:16px; font-weight:700; line-height:24px; color:#2E3842; }
.eh .etm .eu{ font-weight:500; color:#4B5560; font-variant-numeric:tabular-nums; }
.eh .eclk{ position:absolute; right:56px; top:0; font-size:44px; font-weight:800; line-height:58px; letter-spacing:-1px; font-variant-numeric:tabular-nums; }
.eh .ewifi{ position:absolute; right:0; top:14px; width:42px; height:30px; }
.eh .ewifi g{ stroke:#2E3842; } .eh .ewifi circle{ fill:#2E3842; }
.eh .ewifi.lv3 g{ stroke:#B9C1C9; } .eh .ewifi.lv3 circle{ fill:#B9C1C9; }

/* ---------- 종합 위험 배너 ---------- */
.ebn{ position:absolute; left:24px; top:76px; width:1374px; height:52px; border-radius:7px; color:#FFF; padding:0 20px; line-height:52px; white-space:nowrap; overflow:hidden;
  text-shadow:0 1px 2px rgba(0,0,0,.34), 0 0 1px rgba(0,0,0,.18); -webkit-transition:background-color .5s; transition:background-color .5s; }
.ebn.lv0{ background:#57606B; } .ebn.lv1{ background:#E29E00; } .ebn.lv2{ background:#E55A00; } .ebn.lv3{ background:#D21018; }
.ebn .bi{ display:inline-block; width:32px; height:32px; vertical-align:-8px; margin-right:12px; }
.ebn .bi svg{ width:32px; height:32px; stroke-width:2.2; filter:drop-shadow(0 1px 2px rgba(0,0,0,.3)); }
.ebn .bt1{ font-size:22px; font-weight:700; margin-right:12px; }
.ebn b{ font-size:28px; font-weight:800; } .ebn em{ font-style:normal; font-size:22px; font-weight:700; margin-left:8px; }
.ebn .bs{ position:absolute; right:20px; top:0; font-size:18px; font-weight:700; }

/* ---------- 공통 칸 ---------- */
.ee .c{ position:absolute; top:0; bottom:0; padding:16px 20px; border-left:1px solid #AEB6BE; }
.ee .rail{ position:absolute; left:0; top:0; bottom:0; width:10px; -webkit-transition:background-color .5s; transition:background-color .5s; }
.ee .rail.lv0{ background:#57606B; } .ee .rail.lv1{ background:#F4AC00; } .ee .rail.lv2{ background:#E55A00; } .ee .rail.lv3{ background:#D21018; }
.ee .cm{ -webkit-transition:background-color .4s; transition:background-color .4s; }
.ee .cm.lv1{ background:#F8EDCD; } .ee .cm.lv2{ background:#F8E0D2; } .ee .cm.lv3{ background:#F6D8D8; }
.ee .esp{ position:absolute; left:20px; right:20px; bottom:16px; height:58px; width:auto; }
.ee .cm .vv{ margin-top:6px; }
.ee .cw .win{ position:absolute; }
.ee .wr .k{ font-size:15px; font-weight:800; color:#3E4852; }
.ee .wr .num{ display:block; }
.ee .wr .tg{ font-size:15px; font-weight:700; color:#4B5560; margin-top:2px; }
.ee .wr .st{ font-size:17px; font-weight:800; margin-top:4px; }
.ee .wb{ position:absolute; height:10px; border-radius:5px; background:#C3C9CF; }
.ee .wb b{ position:absolute; left:0; top:0; bottom:0; width:0; border-radius:5px; -webkit-transition:width .3s linear, background-color .4s; transition:width .3s linear, background-color .4s; }
.ee .wb i{ position:absolute; top:50%; width:22px; height:22px; margin:-11px 0 0 -11px; border-radius:11px; background:#FFF; border:4px solid #57606B; box-shadow:0 1px 3px rgba(0,0,0,.3); -webkit-transition:left .3s linear; transition:left .3s linear; }
.ee .lc{ color:#FFF; text-shadow:0 1px 2px rgba(0,0,0,.34), 0 0 1px rgba(0,0,0,.18); -webkit-transition:background-color .5s; transition:background-color .5s; }
.ee .lc.lv0{ background:#57606B; } .ee .lc.lv1{ background:#E29E00; } .ee .lc.lv2{ background:#E55A00; } .ee .lc.lv3{ background:#D21018; }

/* ---------- 아래 공용 카드 ---------- */
.ebot{ position:absolute; left:24px; top:634px; width:1374px; height:152px; }
.ebot .ec{ position:absolute; top:0; width:265px; height:152px; background:#EAECEE; border:1px solid #7E8892; border-radius:7px; padding:12px 18px; overflow:hidden; }
.ebot .el .chip{ margin-left:10px; font-size:14px; }
.ebot .el .c2{ position:absolute; right:12px; top:15px; font-size:13px; padding:1px 9px; line-height:20px; }
.ebot .evv{ margin-top:2px; } .ebot .evv .num{ font-size:46px; line-height:52px; } .ebot .evv .u{ font-size:18px; }
.ebot .cc{ position:absolute; right:14px; top:62px; font-size:13px; padding:1px 9px; line-height:20px; }
.ebot .esp{ left:18px; right:18px; bottom:10px; height:40px; }
.ebot .fan{ position:absolute; left:18px; top:54px; width:86px; height:86px; }
.ebot .fr{ position:absolute; left:120px; top:62px; }
.ee .fbig{ display:inline-block; font-size:32px; font-weight:800; line-height:44px; padding:0 14px; border-radius:7px; color:#57606B; }
.ee .fbig.lv1{ background:#57606B; color:#FFF; text-shadow:0 1px 2px rgba(0,0,0,.34), 0 0 1px rgba(0,0,0,.18); }
.ee .fst{ font-size:14px; font-weight:700; margin-top:6px; color:#3E4852; }

/* ===================== E1 : 스트립 ===================== */
.e1 .es{ position:absolute; left:24px; width:1374px; height:236px; background:#EAECEE; border:1px solid #7E8892; border-radius:7px; overflow:hidden; }
.e1 .zc{ left:10px; width:230px; border-left:0; padding:18px 22px; }
.e1 .zt{ font-size:44px; font-weight:700; letter-spacing:-1.5px; line-height:52px; white-space:nowrap; }
.e1 .zi{ display:inline-block; width:34px; height:34px; margin-right:8px; vertical-align:-2px; color:#3E4852; }
.e1 .zi svg{ width:34px; height:34px; }
.e1 .zc .chip{ display:block; margin-top:10px; }
.e1 .zc .zw{ margin-top:4px; font-size:15px; font-weight:700; color:#3E4852; }
.e1 .zsum{ position:absolute; left:22px; right:12px; bottom:16px; font-size:15px; font-weight:800; line-height:20px; letter-spacing:-.3px; }
.e1 .cm .num{ font-size:84px; line-height:88px; } .e1 .cm.tv .num{ font-size:68px; }
.e1 .cw .win{ left:14px; top:56px; width:150px; height:113px; }
.e1 .cw .wr{ position:absolute; left:178px; top:52px; right:8px; }
.e1 .cw .wr .num{ font-size:46px; line-height:50px; }
.e1 .cw .wb{ left:20px; right:20px; bottom:22px; }
.e1 .lc{ left:1140px; width:234px; border-left:0; padding:18px 22px; }
.e1 .lc .k{ font-size:16px; font-weight:800; }
.e1 .lc .bi{ display:inline-block; width:24px; height:24px; vertical-align:-6px; margin-right:8px; }
.e1 .lc .bi svg{ width:24px; height:24px; }
.e1 .lc .lvn{ font-size:60px; font-weight:700; line-height:72px; margin-top:4px; }
.e1 .lc .lvs{ font-size:18px; font-weight:800; }
.e1 .lc .du{ display:block; font-size:14px; font-weight:700; margin-top:4px; font-variant-numeric:tabular-nums; }
.e1 .lc .ix{ position:absolute; left:22px; right:14px; bottom:16px; font-size:15px; font-weight:800; line-height:19px; }

/* ===================== E2 : 세로 구역 ===================== */
.e2 .ez{ position:absolute; top:140px; width:681px; height:484px; background:#EAECEE; border:1px solid #7E8892; border-radius:7px; overflow:hidden; -webkit-transition:box-shadow .4s; transition:box-shadow .4s; }
.e2 .ez.lv1{ box-shadow:0 0 0 3px #F4AC00; } .e2 .ez.lv2{ box-shadow:0 0 0 3px #E55A00; } .e2 .ez.lv3{ box-shadow:0 0 0 3px #D21018; }
.e2 .rail{ width:8px; }
.e2 .zh{ position:absolute; left:8px; right:0; top:0; height:62px; padding:0 20px; line-height:62px; border-bottom:1px solid #AEB6BE; white-space:nowrap; }
.e2 .zh .zi{ display:inline-block; width:34px; height:34px; vertical-align:-7px; margin-right:10px; color:#3E4852; }
.e2 .zh .zi svg{ width:34px; height:34px; }
.e2 .zh b{ font-size:32px; font-weight:700; letter-spacing:-1px; }
.e2 .zh .chip{ margin-left:16px; vertical-align:4px; }
.e2 .zh .zw{ position:absolute; right:20px; top:0; font-size:15px; font-weight:700; color:#3E4852; }
.e2 .ez.lv1 .zh b{ color:#C98A00; } .e2 .ez.lv2 .zh b{ color:#E05200; } .e2 .ez.lv3 .zh b{ color:#D21018; }
.e2 .ez.lv1 .zh .zi{ color:#C98A00; } .e2 .ez.lv2 .zh .zi{ color:#E05200; } .e2 .ez.lv3 .zh .zi{ color:#D21018; }
.e2 .cm{ bottom:auto; padding:14px 22px; }
.e2 .cm:first-of-type{ border-left:0; }
.e2 .cm .num{ font-size:86px; line-height:92px; } .e2 .cm.tv .num{ font-size:72px; }
.e2 .cm .esp{ left:22px; right:22px; bottom:16px; height:62px; }
.e2 .wrow{ position:absolute; left:8px; right:0; top:276px; height:140px; border-top:1px solid #AEB6BE; }
.e2 .wrow .win{ position:absolute; left:20px; top:10px; width:160px; height:120px; }
.e2 .wrow .wr{ position:absolute; left:206px; top:14px; right:20px; }
.e2 .wrow .wr .k{ display:inline-block; }
.e2 .wrow .wr .num{ display:inline-block; font-size:56px; line-height:60px; margin-left:18px; vertical-align:-14px; }
.e2 .wrow .wr .tg,.e2 .wrow .wr .st{ display:inline-block; margin-left:16px; }
.e2 .wrow .wb{ left:206px; right:28px; top:96px; }
.e2 .zst{ position:absolute; left:18px; right:10px; bottom:10px; height:56px; border-radius:7px; color:#FFF; padding:0 18px; line-height:56px; white-space:nowrap; overflow:hidden;
  text-shadow:0 1px 2px rgba(0,0,0,.34), 0 0 1px rgba(0,0,0,.18); -webkit-transition:background-color .5s; transition:background-color .5s; }
.e2 .zst.lv0{ background:#57606B; } .e2 .zst.lv1{ background:#E29E00; } .e2 .zst.lv2{ background:#E55A00; } .e2 .zst.lv3{ background:#D21018; }
.e2 .zst .bi{ display:inline-block; width:30px; height:30px; vertical-align:-8px; margin-right:12px; }
.e2 .zst .bi svg{ width:30px; height:30px; stroke-width:2.2; }
.e2 .zst b{ font-size:28px; font-weight:700; } .e2 .zst em{ font-style:normal; font-size:20px; font-weight:700; margin-left:12px; }
.e2 .zst .zc2{ margin-left:22px; font-size:16px; font-weight:700; }
.e2 .zst .zd{ position:absolute; right:18px; top:0; font-size:15px; font-weight:700; font-variant-numeric:tabular-nums; }

/* ---------- 두 구역 모두 위험: 배경 붉은 점멸 ---------- */
@-webkit-keyframes ebg{ 0%,49.9%{ background-color:#B2B9C0; } 50%,100%{ background-color:#D21018; } }
@keyframes ebg{ 0%,49.9%{ background-color:#B2B9C0; } 50%,100%{ background-color:#D21018; } }
@-webkit-keyframes etx{ 0%,49.9%{ color:#1F2933; } 50%,100%{ color:#FFFFFF; } }
@keyframes etx{ 0%,49.9%{ color:#1F2933; } 50%,100%{ color:#FFFFFF; } }
#stage.ee.blk{ -webkit-animation:ebg 1s linear infinite; animation:ebg 1s linear infinite; }
#stage.ee.blk .eh .elogo,#stage.ee.blk .eh .eclk,#stage.ee.blk .eh .etm,#stage.ee.blk .eh .etm .eu{ -webkit-animation:etx 1s linear infinite; animation:etx 1s linear infinite; }
.e1 .cm .esp{ width:258px; } .e2 .cm .esp{ width:290px; } .ebot .esp{ width:227px; }
/* ===================== F : E2 구역 카드 + D 상단 + D 하단 (이어진 한 줄) ===================== */
.f .hdr .clk{ font-size:52px; font-weight:900; color:#0F1720; }
.f .hdr .dt{ color:#2E3842; }
.f .hdr .wifi g{ stroke:#2E3842; } .f .hdr .wifi circle{ fill:#2E3842; }
.f .hdr .brand b.lv0{ color:#3E4852; }
.f .hdr .pill{ border-radius:7px; box-shadow:none; border:1px solid #57606B; color:#FFF; text-shadow:0 1px 2px rgba(0,0,0,.34), 0 0 1px rgba(0,0,0,.18); }
.f .hdr .pill.lv0{ background:#57606B; } .f .hdr .pill.lv1{ background:#E29E00; border-color:#E29E00; } .f .hdr .pill.lv2{ background:#E55A00; border-color:#E55A00; } .f .hdr .pill.lv3{ background:#D21018; border-color:#D21018; }
.f .hdr .mn{ background:#EAECEE; border:1px solid #7E8892; border-radius:7px; }
.f .hdr .conn .cd.lv0{ background:#2E3842; }
.f .ez{ top:92px; height:502px; }
.f .wrow{ top:292px; }
.f .cm .num{ font-size:92px; line-height:98px; } .f .cm.tv .num{ font-size:76px; }
.f .cm .esp{ bottom:18px; height:66px; }
.f .zh .zi{ width:36px; height:36px; vertical-align:-8px; }
.f .zh .zi svg{ width:36px; height:36px; fill:currentColor; stroke:none; }
.f .zh .zi .smk{ opacity:.45; }
/* 아래 공용 줄: D와 같이 하나로 이어진 판 */
.fbot{ position:absolute; left:24px; top:606px; width:1374px; height:178px; background:#EAECEE; border:1px solid #7E8892; border-radius:7px; overflow:hidden; }
.fbot .fc{ position:absolute; top:0; bottom:0; padding:16px 22px; border-left:1px solid #AEB6BE; }
.fbot .fv{ margin-top:6px; } .fbot .fv .num{ font-size:54px; line-height:58px; }
.fbot .fbar{ position:absolute; left:22px; right:22px; bottom:40px; height:10px; border-radius:3px; background:#C3C9CF; overflow:hidden; }
.fbot .fbar b{ position:absolute; left:0; top:0; bottom:0; width:0; border-radius:3px; -webkit-transition:width .6s, background-color .4s; transition:width .6s, background-color .4s; }
.fbot .fbar s{ position:absolute; top:0; bottom:0; width:2px; margin-left:-1px; background:#EAECEE; }
.fbot .ftk{ position:absolute; left:22px; right:22px; bottom:14px; height:18px; font-size:13px; font-weight:700; color:#2E3842; }
.fbot .ftk span{ position:absolute; width:70px; margin-left:-35px; text-align:center; }
.fbot .fcc{ position:absolute; right:18px; top:20px; font-size:14px; padding:2px 10px; line-height:20px; }
.fbot .lab .chip{ margin-left:10px; font-size:14px; }
.fbot .fan{ position:absolute; left:22px; top:58px; width:104px; height:104px; }
.fbot .fr{ position:absolute; left:150px; top:72px; }
#stage.ee.f.blk .hdr .clk,#stage.ee.f.blk .hdr .dt,#stage.ee.f.blk .hdr .brand,#stage.ee.f.blk .hdr .conn,#stage.ee.f.blk .hdr .conn .upd{ -webkit-animation:etx 1s linear infinite; animation:etx 1s linear infinite; }
.fbot .esp{ left:22px; right:auto; bottom:14px; height:54px; }
/* 창문: 톱니·랙 기어 제거, 그림 키우고 개도율 막대는 짧게 */
.f .wrow .win{ left:22px; top:8px; width:216px; height:130px; }
.f .wrow .wr{ left:262px; top:16px; }
.f .wrow .wb{ left:262px; right:44px; top:98px; }

/* 밝은 박스 뒤 2px 검정 번짐 그림자 */
.f .ez{ box-shadow:0 1px 2px rgba(0,0,0,.35); }
.f .ez.lv1{ box-shadow:0 0 0 3px #F4AC00, 0 1px 2px rgba(0,0,0,.35); }
.f .ez.lv2{ box-shadow:0 0 0 3px #E55A00, 0 1px 2px rgba(0,0,0,.35); }
.f .ez.lv3{ box-shadow:0 0 0 3px #D21018, 0 1px 2px rgba(0,0,0,.35); }
.fbot,.f .hdr .mn{ box-shadow:0 1px 2px rgba(0,0,0,.35); }
/* =====================================================================
   H : G 배치 + 모노톤 소프트 UI (흰 카드, 큰 라운드, 부드러운 그림자, 검정 포인트)
   경보 원인 칸은 '검은 카드'로 반전, 정상 색은 차콜
   ===================================================================== */
#stage.ee.hh{ background:#E7E7E9; color:#1C1C1E; }
.hh .num{ color:#1C1C1E; letter-spacing:-.04em; }
.hh .u{ color:#8E8E93; font-weight:600; }
.hh .nid{ color:#8E8E93; }
.hh .lab,.hh .el{ font-size:16px; font-weight:700; color:#1C1C1E; letter-spacing:-.2px; }
.hh .lab small{ color:#9A9AA0; font-weight:500; }
.hh .ic{ width:38px; height:38px; border:0; border-radius:12px; background:#F1F1F3; color:#1C1C1E; padding:6px; margin-right:12px; }
.hh .chip{ font-size:13px; font-weight:700; color:#8E8E93; } .hh .chip i{ border-radius:5px; background:#34C759; width:8px; height:8px; }
.hh .chip.bad{ color:#D21018; } .hh .chip.bad i{ background:#D21018; }
/* 헤더 */
.hh .hdr .clk{ font-size:48px; font-weight:800; letter-spacing:-2px; color:#1C1C1E; }
.hh .hdr .dt{ color:#8E8E93; font-weight:600; } .hh .hdr .brand{ font-weight:800; color:#1C1C1E; } .hh .hdr .brand b.lv0{ color:#1C1C1E; }
.hh .hdr .wifi g{ stroke:#1C1C1E; } .hh .hdr .wifi circle{ fill:#1C1C1E; }
.hh .hdr .pill{ border-radius:16px; border:0; box-shadow:0 10px 24px rgba(0,0,0,.14); font-size:24px; }
.hh .hdr .pill.lv0{ background:#2B2B2D; }
.hh .hdr .pill span{ opacity:.7; }
.hh .hdr .mn{ background:#FFF; border:0; border-radius:16px; box-shadow:0 8px 24px rgba(0,0,0,.06) !important; }
.hh .hdr .conn{ color:#1C1C1E; } .hh .hdr .conn .upd{ color:#8E8E93; } .hh .hdr .conn .cd.lv0{ background:#34C759; }
/* 구역 카드 */
.hh .ez{ background:#FFFFFF; border:0; border-radius:28px; box-shadow:0 18px 40px rgba(0,0,0,.07), 0 1px 2px rgba(0,0,0,.06) !important; }
.hh .ez.lv1{ box-shadow:0 0 0 3px #F4AC00, 0 18px 40px rgba(0,0,0,.07) !important; }
.hh .ez.lv2{ box-shadow:0 0 0 3px #E55A00, 0 18px 40px rgba(0,0,0,.07) !important; }
.hh .ez.lv3{ box-shadow:0 0 0 3px #D21018, 0 18px 40px rgba(0,0,0,.07) !important; }
.hh .rail{ display:none; }
.hh .zh{ left:0; border-bottom:0; padding:0 26px; height:70px; line-height:74px; }
.hh .zh .zi{ width:42px; height:42px; padding:8px; border-radius:13px; background:#F1F1F3; vertical-align:-10px; margin-right:14px; color:#1C1C1E; }
.hh .zh .zi svg{ width:26px; height:26px; display:block; }
.hh .zh b{ font-size:34px; font-weight:800; letter-spacing:-1.2px; }
.hh .zh .chip{ vertical-align:6px; }
.hh .zh .zw{ right:86px; font-size:13px; color:#8E8E93; }
.hh .ez.lv1 .zh b{ color:#1C1C1E; } .hh .ez.lv2 .zh b{ color:#1C1C1E; } .hh .ez.lv3 .zh b{ color:#1C1C1E; }
.hh .ez.lv1 .zh .zi{ background:#F4AC00; color:#FFF; } .hh .ez.lv2 .zh .zi{ background:#E55A00; color:#FFF; } .hh .ez.lv3 .zh .zi{ background:#D21018; color:#FFF; }
.hh .rg{ position:absolute; right:22px; top:12px; width:50px; height:50px; }
.hh .rg svg{ position:absolute; left:0; top:0; width:50px; height:50px; }
.hh .rg b{ position:absolute; left:0; right:0; top:0; line-height:50px; text-align:center; font-size:15px; font-weight:800; color:#1C1C1E; }
.hh .cm{ border-left:0 !important; border-radius:22px; margin:0; -webkit-transition:background-color .4s; transition:background-color .4s; }
.hh .cm[style*="left:10px"]{ left:16px !important; width:318px !important; }
.hh .cm.tv{ left:346px !important; width:319px !important; }
.hh .cm{ top:72px !important; height:212px !important; background:#F6F6F8; }
.hh .cm.lv1,.hh .cm.lv2,.hh .cm.lv3{ background:#232325; box-shadow:0 14px 30px rgba(0,0,0,.18); }
.hh .cm.lv1 .lab,.hh .cm.lv2 .lab,.hh .cm.lv3 .lab{ color:#FFF; }
.hh .cm.lv1 .ic,.hh .cm.lv2 .ic,.hh .cm.lv3 .ic{ background:#3A3A3D; color:#FFF; }
.hh .cm.lv1 .u,.hh .cm.lv2 .u,.hh .cm.lv3 .u{ color:#A0A0A6; }
.hh .cm .esp{ width:276px !important; left:21px !important; }
.hh .cm .num{ font-size:84px; line-height:92px; } .hh .cm.tv .num{ font-size:70px; }
.hh .wrow{ left:0; top:296px; border-top:0; }
.hh .wrow .k{ color:#8E8E93; font-weight:700; }
.hh .wrow .tg{ color:#8E8E93; } .hh .wrow .st{ color:#1C1C1E; }
.hh .wb{ background:#ECECEE; height:8px; border-radius:4px; }
.hh .wb i{ background:#FFF; box-shadow:0 2px 6px rgba(0,0,0,.25); }
.hh .zst{ left:16px; right:16px; bottom:14px; height:58px; line-height:58px; border-radius:18px; box-shadow:0 10px 22px rgba(0,0,0,.14); }
.hh .zst.lv0{ background:#2B2B2D; }
.hh .zst .zd:after{ content:'  →'; }
/* 아래 공용 줄 */
.hh .fbot{ background:#FFFFFF; border:0; border-radius:28px; box-shadow:0 18px 40px rgba(0,0,0,.07), 0 1px 2px rgba(0,0,0,.06) !important; }
.hh .fbot .fc{ border-left:1px solid #EFEFF1; padding:18px 24px; }
.hh .fbot .fcc{ border-radius:10px; }
.hh .c2.lv0{ background:#F1F1F3; color:#1C1C1E; }
.hh .fbot .fv .num{ font-size:50px; }
.hh .fbig{ border-radius:14px; } .hh .fbig.lv1{ background:#2B2B2D; }
.hh .fst{ color:#8E8E93; }
@-webkit-keyframes hbg{ 0%,49.9%{ background-color:#E7E7E9; } 50%,100%{ background-color:#D21018; } }
@keyframes hbg{ 0%,49.9%{ background-color:#E7E7E9; } 50%,100%{ background-color:#D21018; } }
#stage.ee.hh.blk{ -webkit-animation:hbg 1s linear infinite; animation:hbg 1s linear infinite; }
.hh .wrow{ top:290px; }
.hh .wrow .win{ left:24px; top:4px; width:200px; height:120px; }
.hh .wrow .wr{ left:250px; top:10px; } .hh .wrow .wb{ left:250px; top:92px; }
/* ===================== H1 / H2 공통: 왼쪽 어두운 사이드바 + 본문 축소 ===================== */
.sb{ display:none; }
#stage.nx .sb,#stage.nx2 .sb{ display:block; position:absolute; left:0; top:0; bottom:0; width:116px; background:#1C1C21; color:#9A9CA5; padding:18px 10px; }
.sb .sbl{ width:44px; height:44px; margin:0 auto 18px; color:#F26B1D; }
.sb .sbl svg{ width:44px; height:44px; display:block; }
.sb .sbt{ font-size:11px; font-weight:700; letter-spacing:.12em; color:#6B6E78; margin:14px 8px 6px; }
.sb .sbi{ height:42px; border-radius:9px; padding:0 10px; font-size:15px; font-weight:700; line-height:42px; white-space:nowrap; margin-bottom:4px; }
.sb .sbi svg{ width:20px; height:20px; vertical-align:-4px; margin-right:9px; fill:none; stroke:currentColor; stroke-width:1.9; stroke-linecap:round; stroke-linejoin:round; }
.sb .sbi.on{ background:#F26B1D; color:#FFF; box-shadow:0 6px 16px rgba(242,107,29,.35); }
.sb .sbb{ position:absolute; left:10px; right:8px; bottom:18px; font-size:12px; font-weight:600; line-height:16px; color:#8A8C95; }
.sb .sbb i{ display:inline-block; width:8px; height:8px; border-radius:4px; margin-right:6px; }
.sb .sbb i.lv0{ background:#34C759; } .sb .sbb i.lv3{ background:#FF5257; }
#stage.nx .cw,#stage.nx2 .cw{ position:absolute; left:124px; top:37px; width:1422px; height:800px; -webkit-transform:scale(.9058); transform:scale(.9058); -webkit-transform-origin:0 0; transform-origin:0 0; }
.dl{ position:absolute; right:20px; top:20px; padding:2px 9px; border-radius:7px; font-size:13px; font-weight:800; line-height:20px; font-variant-numeric:tabular-nums; }
.dl:after{ content:' 1분'; font-weight:600; opacity:.75; }
.dl.lv0{ background:#F1F2F4; color:#6B717B; } .dl.lv1{ background:#FDECEC; color:#D9363E; } .dl.lv2{ background:#E6F6EC; color:#1A9A52; }
.fbot .dl{ top:66px; right:18px; }

/* ===================== H1 : G 배치 + 참고 대시보드 (흰 카드, 오렌지 포인트, 어두운 사이드바) ===================== */
#stage.ee.nx{ background:#F4F5F7; }
.nx .hdr .clk{ font-size:50px; font-weight:800; color:#1F2329; letter-spacing:-1.5px; }
.nx .hdr .dt{ color:#8A8F98; } .nx .hdr .brand{ color:#1F2329; } .nx .hdr .brand b.lv0{ color:#1A9A52; }
.nx .hdr .wifi g{ stroke:#1F2329; } .nx .hdr .wifi circle{ fill:#1F2329; }
.nx .hdr .pill{ border-radius:10px; border:0; box-shadow:0 6px 18px rgba(16,24,40,.12); }
.nx .hdr .pill.lv0{ background:#1C1C21; }
.nx .hdr .mn{ background:#FFF; border:1px solid #ECEDF0; border-radius:10px; box-shadow:0 2px 10px rgba(16,24,40,.05) !important; }
.nx .hdr .conn .cd.lv0{ background:#34C759; }
.nx .ez{ background:#FFF; border:1px solid #ECEDF0; border-radius:12px; box-shadow:0 2px 12px rgba(16,24,40,.06); }
.nx .ez.lv1{ box-shadow:0 0 0 3px #F4AC00, 0 2px 12px rgba(16,24,40,.06); } .nx .ez.lv2{ box-shadow:0 0 0 3px #E55A00, 0 2px 12px rgba(16,24,40,.06); } .nx .ez.lv3{ box-shadow:0 0 0 3px #D21018, 0 2px 12px rgba(16,24,40,.06); }
.nx .rail{ display:none; }
.nx .zh{ left:0; padding-left:26px; border-bottom:1px solid #F0F1F3; }
.nx .zh:before{ content:''; position:absolute; left:0; top:18px; width:4px; height:26px; border-radius:0 3px 3px 0; background:#F26B1D; }
.nx .zh .zi{ width:40px; height:40px; padding:8px; border-radius:20px; background:#F26B1D; color:#FFF; vertical-align:-12px; box-shadow:0 4px 10px rgba(242,107,29,.3); }
.nx .zh .zi svg{ width:24px; height:24px; display:block; }
.nx .zh .zi .smk{ opacity:.6; }
.nx .ez.lv1 .zh .zi{ background:#F4AC00; } .nx .ez.lv2 .zh .zi{ background:#E55A00; } .nx .ez.lv3 .zh .zi{ background:#D21018; }
.nx .zh b{ font-weight:800; color:#1F2329; }
.nx .ez.lv1 .zh b,.nx .ez.lv2 .zh b,.nx .ez.lv3 .zh b{ color:#1F2329; }
.nx .zh .zw{ right:86px; color:#8A8F98; }
.nx .rg{ position:absolute; right:20px; top:6px; width:50px; height:50px; }
.nx .rg svg{ position:absolute; left:0; top:0; width:50px; height:50px; }
.nx .rg b{ position:absolute; left:0; right:0; top:0; line-height:50px; text-align:center; font-size:15px; font-weight:800; }
.nx .cm{ border-left:1px solid #F0F1F3; }
.nx .cm[style*="left:10px"]{ border-left:0; }
.nx .ic{ width:38px; height:38px; border:0; border-radius:19px; background:#FFF1E7; color:#F26B1D; padding:6px; }
.nx .lab,.nx .el{ font-size:16px; color:#1F2329; } .nx .lab small{ color:#8A8F98; }
.nx .num{ color:#1F2329; } .nx .u{ color:#8A8F98; }
.nx .wrow{ border-top:1px solid #F0F1F3; }
.nx .wrow .k,.nx .wrow .tg{ color:#8A8F98; }
.nx .wb{ background:#F1F2F4; height:8px; }
.nx .zst{ border-radius:10px; box-shadow:0 8px 20px rgba(16,24,40,.12); }
.nx .zst.lv0{ background:linear-gradient(90deg, #1C1C21, #3A3B44); }
.nx .zst.lv1{ background:linear-gradient(90deg, #E8A100, #F6C24E); } .nx .zst.lv2{ background:linear-gradient(90deg, #E55A00, #F58A45); } .nx .zst.lv3{ background:linear-gradient(90deg, #D21018, #EA4A50); }
.nx .fbot{ background:#FFF; border:1px solid #ECEDF0; border-radius:12px; box-shadow:0 2px 12px rgba(16,24,40,.06); }
.nx .fbot .fc{ border-left-color:#F0F1F3; }
.nx .fbig.lv1{ background:#1C1C21; } .nx .fbig{ border-radius:8px; }
.nx .chip i{ border-radius:5px; background:#34C759; } .nx .chip{ color:#6B717B; }
.nx .chip.bad i{ background:#D21018; } .nx .chip.bad{ color:#D21018; }
.nx .c2.lv0{ background:#E6F6EC; color:#1A9A52; }
@-webkit-keyframes nxbg{ 0%,49.9%{ background-color:#F4F5F7; } 50%,100%{ background-color:#D21018; } }
@keyframes nxbg{ 0%,49.9%{ background-color:#F4F5F7; } 50%,100%{ background-color:#D21018; } }
#stage.ee.nx.blk{ -webkit-animation:nxbg 1s linear infinite; animation:nxbg 1s linear infinite; }

/* ===================== H2 : H + 참고 대시보드 종합 ===================== */
#stage.ee.hh.nx2{ background:#ECECEE; }
.nx2 .zh:before{ content:''; position:absolute; left:0; top:22px; width:4px; height:28px; border-radius:0 3px 3px 0; background:#F26B1D; }
.nx2 .ic{ background:#FFF1E7; color:#F26B1D; border-radius:19px; }
.nx2 .cm.lv1 .ic,.nx2 .cm.lv2 .ic,.nx2 .cm.lv3 .ic{ background:#3A3A3D; color:#FFF; }
.nx2 .zh .zi{ background:#FFF1E7; color:#F26B1D; border-radius:21px; }
.nx2 .cm.lv1 .dl,.nx2 .cm.lv2 .dl,.nx2 .cm.lv3 .dl{ background:#3A3A3D; }
.nx2 .cm.lv1 .dl.lv1,.nx2 .cm.lv2 .dl.lv1,.nx2 .cm.lv3 .dl.lv1{ color:#FF8A8F; }
.nx2 .cm.lv1 .dl.lv2,.nx2 .cm.lv2 .dl.lv2,.nx2 .cm.lv3 .dl.lv2{ color:#5BD38C; }
.nx2 .cm.lv1 .dl.lv0,.nx2 .cm.lv2 .dl.lv0,.nx2 .cm.lv3 .dl.lv0{ color:#C7C7CC; }
.nx2 .zst.lv0{ background:linear-gradient(90deg, #232325, #3E3E43); }
.nx2 .zst.lv1{ background:linear-gradient(90deg, #E8A100, #F6C24E); } .nx2 .zst.lv2{ background:linear-gradient(90deg, #E55A00, #F58A45); } .nx2 .zst.lv3{ background:linear-gradient(90deg, #D21018, #EA4A50); }
.nx2 .hdr .pill.lv0{ background:linear-gradient(90deg, #232325, #3E3E43); }
.nx2 .wb b{ box-shadow:0 0 8px rgba(0,0,0,.08); }
@-webkit-keyframes nx2bg{ 0%,49.9%{ background-color:#ECECEE; } 50%,100%{ background-color:#D21018; } }
@keyframes nx2bg{ 0%,49.9%{ background-color:#ECECEE; } 50%,100%{ background-color:#D21018; } }
#stage.ee.hh.nx2.blk{ -webkit-animation:nx2bg 1s linear infinite; animation:nx2bg 1s linear infinite; }
.nx .ez.lv1 .zh .zi,.nx .ez.lv2 .zh .zi,.nx .ez.lv3 .zh .zi{ color:#FFF; }
.fbot .dl{ top:18px; right:16px; }
.fbot .fcc{ top:54px !important; right:16px !important; }
.sb .sbb{ white-space:normal; }
.nx2 .cm .dl:after{ content:''; }
/* ===================== I : H2에서 사이드바·제목 막대 제거, 아이콘은 H로, 모서리는 직각에 가깝게 ===================== */
#stage.ii .zh:before{ display:none; }
#stage.ii .ic{ background:#F1F1F3; color:#1C1C1E; border-radius:8px; }
#stage.ii .cm.lv1 .ic,#stage.ii .cm.lv2 .ic,#stage.ii .cm.lv3 .ic{ background:#3A3A3D; color:#FFF; }
#stage.ii .zh .zi{ background:#F1F1F3; color:#1C1C1E; border-radius:9px; }
#stage.ii .ez.lv1 .zh .zi{ background:#F4AC00; color:#FFF; } #stage.ii .ez.lv2 .zh .zi{ background:#E55A00; color:#FFF; } #stage.ii .ez.lv3 .zh .zi{ background:#D21018; color:#FFF; }
#stage.ii .ez,#stage.ii .fbot{ border-radius:12px; }
#stage.ii .cm{ border-radius:9px; }
#stage.ii .zst{ border-radius:9px; }
#stage.ii .hdr .pill,#stage.ii .hdr .mn{ border-radius:9px; }
#stage.ii .fbig{ border-radius:7px; }
#stage.ii .dl{ border-radius:5px; }
#stage.ii .c2,#stage.ii .fbot .fcc{ border-radius:6px; }
#stage.ii .wb,#stage.ii .wb b{ border-radius:3px; }
/* 변화량: 최근 10초 기준, 오르면 빨강 / 내리면 초록 (흰 칸, 검은 칸 모두) */
.dl:after{ content:' 10초'; }
#stage.ii .cm .dl:after{ content:''; }
#stage.ii .dl.lv1{ background:#FDECEC; color:#D9363E; } #stage.ii .dl.lv2{ background:#E6F6EC; color:#1A9A52; } #stage.ii .dl.lv0{ background:#F1F2F4; color:#6B717B; }
#stage.ii .cm.lv1 .dl,#stage.ii .cm.lv2 .dl,#stage.ii .cm.lv3 .dl{ background:#3A3A3D; }
#stage.ii .cm.lv1 .dl.lv1,#stage.ii .cm.lv2 .dl.lv1,#stage.ii .cm.lv3 .dl.lv1{ color:#FF8A8F; }
#stage.ii .cm.lv1 .dl.lv2,#stage.ii .cm.lv2 .dl.lv2,#stage.ii .cm.lv3 .dl.lv2{ color:#5BD38C; }
#stage.ii .cm.lv1 .dl.lv0,#stage.ii .cm.lv2 .dl.lv0,#stage.ii .cm.lv3 .dl.lv0{ color:#C7C7CC; }
/* 등급 띠 그라데이션 폭을 줄여 오른쪽이 덜 밝아지게 */
#stage.ii .zst.lv0{ background:linear-gradient(90deg, #232325, #2F2F33); }
#stage.ii .zst.lv1{ background:linear-gradient(90deg, #E8A100, #EDB12C); }
#stage.ii .zst.lv2{ background:linear-gradient(90deg, #E55A00, #EA6D1D); }
#stage.ii .zst.lv3{ background:linear-gradient(90deg, #D21018, #DA282F); }
#stage.ii .hdr .pill.lv0{ background:linear-gradient(90deg, #232325, #2F2F33); }
#stage.ii .fbot .dl:after{ content:''; }
/* J: 창문 그림 교체 (얇은 선 + 둥근 모서리), 바람은 그라데이션 곡선 */
.jj .wrow .win{ filter:drop-shadow(0 4px 10px rgba(28,28,30,.08)); }
#stage.jj .zst.lv0{ background:linear-gradient(90deg, #36383D, #404247); }
/* K: J1의 검은 카드들 명도를 한 단계 올림 */
#stage.kk .cm.lv1,#stage.kk .cm.lv2,#stage.kk .cm.lv3{ background:#2F3034; }
#stage.kk .cm.lv1 .ic,#stage.kk .cm.lv2 .ic,#stage.kk .cm.lv3 .ic{ background:#45474C; }
#stage.kk .cm.lv1 .dl,#stage.kk .cm.lv2 .dl,#stage.kk .cm.lv3 .dl{ background:#45474C; }
#stage.kk .hdr .pill.lv0{ background:linear-gradient(90deg, #2F3034, #3A3B40); }
#stage.kk .zst.lv0{ background:linear-gradient(90deg, #3B3D42, #45474C); }
#stage.kk .fbig.lv1{ background:#383A3F; }
/* 원인 검은 카드 명도 +10 */
#stage.kk .cm.lv1,#stage.kk .cm.lv2,#stage.kk .cm.lv3{ background:#45474C; }
#stage.kk .cm.lv1 .ic,#stage.kk .cm.lv2 .ic,#stage.kk .cm.lv3 .ic{ background:#5C5F65; }
#stage.kk .cm.lv1 .dl,#stage.kk .cm.lv2 .dl,#stage.kk .cm.lv3 .dl{ background:#5C5F65; }
/* 정상 상태의 위·아래 긴 띠 명도 +30 : 경보 검은 카드와 확실히 구분 */
#stage.kk .hdr .pill.lv0{ background:linear-gradient(90deg, #6F747C, #7A7F87); }
#stage.kk .zst.lv0{ background:linear-gradient(90deg, #6F747C, #7A7F87); }
#stage.kk .cm.lv1,#stage.kk .cm.lv2,#stage.kk .cm.lv3{ background:linear-gradient(180deg, #45474C, #313337); }
/* K2: 이전 HMI처럼 모든 카드를 직각에 가깝게 (살짝만 둥글게) */
#stage.k2 .ez,#stage.k2 .fbot{ border-radius:6px; }
#stage.k2 .cm{ border-radius:4px; }
#stage.k2 .zst{ border-radius:5px; }
#stage.k2 .hdr .pill,#stage.k2 .hdr .mn{ border-radius:6px; }
#stage.k2 .ic,#stage.k2 .zh .zi{ border-radius:5px; }
#stage.k2 .dl{ border-radius:3px; }
#stage.k2 .fbig{ border-radius:5px; }
#stage.k2 .c2,#stage.k2 .fbot .fcc{ border-radius:4px; }
#stage.k2 .wb,#stage.k2 .wb b{ border-radius:2px; }
/* 등급 띠의 방패·경고 아이콘을 글자 높이 가운데로 */
#stage.kk .zst .bi{ vertical-align:middle; position:relative; top:-6.7px; height:30px; line-height:0; }
#stage.kk .zst .bi i,#stage.kk .zst .bi svg{ display:block; }
#stage.kk .zst .bi .s0,#stage.kk .zst .bi .s1{ display:block; }
#stage.kk .zst.lv0 .bi .s1{ display:none; } #stage.kk .zst.lv1 .bi .s0,#stage.kk .zst.lv2 .bi .s0,#stage.kk .zst.lv3 .bi .s0{ display:none; }
/* 주의·경고·위험 구역 카드 바탕을 해당 단계의 아주 엷은 색으로 */
#stage.kk .ez{ -webkit-transition:background-color .5s; transition:background-color .5s; }
#stage.kk .ez.lv1{ background:#FFF7E6; } #stage.kk .ez.lv2{ background:#FFF0E6; } #stage.kk .ez.lv3{ background:#FFEBEB; }
#stage.kk .ez.lv1 .cm.lv0,#stage.kk .ez.lv2 .cm.lv0,#stage.kk .ez.lv3 .cm.lv0{ background:rgba(255,255,255,.65); }
/* 경보 신호 정리: 카드 테두리 색 제거 (카드 바탕 엷은 색, 검은 원인 칸, 등급 띠, 상단 배너로 충분) */
#stage.kk .ez.lv1,#stage.kk .ez.lv2,#stage.kk .ez.lv3{ box-shadow:0 18px 40px rgba(0,0,0,.07), 0 1px 2px rgba(0,0,0,.06) !important; }
/* 노드 통신 끊김: 카드 전체를 흐린 회색으로, 값은 --, 등급 띠는 '통신 끊김' */
#stage.kk .ez.lost{ background:#E9EAEC !important; box-shadow:0 0 0 2px #8A8F96 !important; }
#stage.kk .ez.lost .cm,#stage.kk .ez.lost .wrow,#stage.kk .ez.lost .rg{ -webkit-filter:grayscale(1); filter:grayscale(1); opacity:.45; }
#stage.kk .ez.lost .cm{ background:#F2F3F4 !important; box-shadow:none !important; }
#stage.kk .ez.lost .cm .num,#stage.kk .ez.lost .cm .lab{ color:#1C1C1E !important; text-shadow:none !important; }
#stage.kk .ez.lost .zh .zi{ background:#8A8F96 !important; color:#FFF !important; }
#stage.kk .ez.lost .zst{ background:repeating-linear-gradient(135deg, #5C5F65 0, #5C5F65 14px, #64676D 14px, #64676D 28px) !important; }
#stage.kk .ez.lost .zst .s0{ display:none !important; } #stage.kk .ez.lost .zst .s1{ display:block !important; }
#stage.kk .ez.lost .zst .zd:after{ content:''; }
/* 정상 칸 → 검은 경보 칸 : 0.45초 디졸브 전환 (그라데이션은 직접 전환이 안 되어 덮개 층의 투명도로 처리) */
#stage.kk .cm{ background:#F6F6F8 !important; -webkit-transition:box-shadow .28s; transition:box-shadow .28s; }
#stage.kk .ez.lv1 .cm.lv0,#stage.kk .ez.lv2 .cm.lv0,#stage.kk .ez.lv3 .cm.lv0{ background:rgba(255,255,255,.65) !important; }
#stage.kk .cm:before{ content:''; position:absolute; left:0; top:0; right:0; bottom:0; border-radius:inherit; background:linear-gradient(180deg, #45474C, #313337);
  opacity:0; -webkit-transition:opacity .28s ease; transition:opacity .28s ease; }
#stage.kk .cm.lv1:before,#stage.kk .cm.lv2:before,#stage.kk .cm.lv3:before{ opacity:1; }
#stage.kk .cm > .lab,#stage.kk .cm > .vv{ position:relative; z-index:1; }
#stage.kk .cm > .esp,#stage.kk .cm > .dl{ z-index:1; }
#stage.kk .cm .lab,#stage.kk .cm .u,#stage.kk .cm .num{ -webkit-transition:color .28s; transition:color .28s; }
#stage.kk .cm .ic,#stage.kk .cm .dl{ -webkit-transition:background-color .28s, color .28s; transition:background-color .28s, color .28s; }
#stage.kk .ez.lost .cm:before{ opacity:0; }
/* 두 구역(노드) 카드 사이 분리감: 그림자를 조금 더 또렷하게 */
#stage.kk .ez,#stage.kk .ez.lv1,#stage.kk .ez.lv2,#stage.kk .ez.lv3{ box-shadow:0 8px 22px rgba(0,0,0,.12), 0 2px 5px rgba(0,0,0,.08) !important; }
/* 등급 띠 : 단계가 바뀔 때 0.28초 디졸브. 그라데이션은 직접 전환이 안 되므로 단색 + 오른쪽 밝아짐 덮개로 같은 모양을 냄 */
#stage.kk .zst{ background-image:none !important; -webkit-transition:background-color .28s ease; transition:background-color .28s ease; }
#stage.kk .zst.lv0{ background-color:#6F747C; } #stage.kk .zst.lv1{ background-color:#E8A100; }
#stage.kk .zst.lv2{ background-color:#E55A00; } #stage.kk .zst.lv3{ background-color:#D21018; }
#stage.kk .zst:after{ content:''; position:absolute; left:0; top:0; right:0; bottom:0; border-radius:inherit; pointer-events:none;
  background:linear-gradient(90deg, rgba(255,255,255,0), rgba(255,255,255,.07)); }
#stage.kk .ez.lost .zst:after{ display:none; }
/* 정상 칸·아이콘 타일 명도 살짝 낮춤 (흰 카드와 구분), 경보 카드 안 밝은 칸은 살짝 높임 */
#stage.kk .cm{ background:#EFEFF2 !important; }
#stage.kk .ic,#stage.kk .zh .zi{ background:#E5E6EA; }
#stage.kk .ez.lv1 .zh .zi{ background:#F4AC00; } #stage.kk .ez.lv2 .zh .zi{ background:#E55A00; } #stage.kk .ez.lv3 .zh .zi{ background:#D21018; }
#stage.kk .cm.lv1 .ic,#stage.kk .cm.lv2 .ic,#stage.kk .cm.lv3 .ic{ background:#5C5F65; }
#stage.kk .dl.lv0{ background:#E5E6EA; }
#stage.kk .cm.lv1 .dl,#stage.kk .cm.lv2 .dl,#stage.kk .cm.lv3 .dl{ background:#5C5F65; }
#stage.kk .ez.lv1 .cm.lv0,#stage.kk .ez.lv2 .cm.lv0,#stage.kk .ez.lv3 .cm.lv0{ background:rgba(255,255,255,.88) !important; }
#stage.kk .ez.lv1 .cm.lv0 .ic,#stage.kk .ez.lv2 .cm.lv0 .ic,#stage.kk .ez.lv3 .cm.lv0 .ic{ background:#ECEDF0; }
/* M : 종합 위험을 두 구역 위험지수 평균으로 판정, 배너 오른쪽에 평균 게이지 */
#stage.mm .hdr .pill{ left:516px; width:394px; text-align:left; padding-left:22px; }
#stage.mm .hdr .pill span{ font-size:18px; margin-right:10px; }
#stage.mm .hdr .pill em{ font-size:16px; }
#stage.mm .hdr .pill.lv0{ background:linear-gradient(90deg, #6F747C, #7A7F87); }
#stage.mm .hdr .pill.lv1{ background:#E8A100; } #stage.mm .hdr .pill.lv2{ background:#E55A00; } #stage.mm .hdr .pill.lv3{ background:#D21018; }
#stage.mm .hdr .pill{ -webkit-transition:background-color .28s; transition:background-color .28s; }
.pga{ position:absolute; right:16px; top:0; height:52px; width:136px; }
.pga .pgb{ position:absolute; left:0; top:22px; width:96px; height:8px; border-radius:4px; background:rgba(255,255,255,.3) !important; }
.pga .pgb b{ background:#FFF !important; border-radius:4px; box-shadow:0 1px 2px rgba(0,0,0,.25); }
.pga .pgb s{ background:rgba(0,0,0,.18); }
.pga .pgv{ position:absolute; right:0; top:0; width:32px; text-align:right; font-size:20px; font-weight:800; line-height:52px; font-variant-numeric:tabular-nums; }
.pga:before{ content:'평균 지수'; position:absolute; left:0; top:3px; font-size:11px; font-weight:700; line-height:14px; opacity:.85; }
/* 검은 경보 칸 주변의 큰 번짐 그림자와 숫자 뒤 검정 번짐 제거 (어두운 바탕에선 필요 없음) */
#stage.kk .cm.lv1,#stage.kk .cm.lv2,#stage.kk .cm.lv3{ box-shadow:none !important; }
#stage.kk .cm.lv1 .num,#stage.kk .cm.lv2 .num,#stage.kk .cm.lv3 .num{ text-shadow:none !important; }
/* N : 항목 이름 글자 키움, 아래 공용 줄 칸 경계선 살짝 진하게 */
#stage.nn .cm .lab,#stage.nn .fbot .lab{ font-size:19px; font-weight:800; }
#stage.nn .cm .lab small,#stage.nn .fbot .lab small{ font-size:15px; }
#stage.nn .fbot .fc{ border-left-color:#D9DBDF; }
/* =====================================================================
   최종 정리 : 디자인 토큰 통일
   회색 5단계  #1C1C1E 글자 / #4B5058 그래프·링·정상 막대 / #6F747C 정상 띠 / #D9DBDF 경계선 / #EFEFF2 칸
   검은 경보 칸 #45474C → #313337 , 경보색 4개(LV_COL) , 모서리 2종(카드 6px · 작은 요소 4px) , 그림자 1종
   ===================================================================== */
#stage.fin .ez,#stage.fin .fbot,#stage.fin .cm,#stage.fin .zst,#stage.fin .hdr .pill,#stage.fin .hdr .mn{ border-radius:6px; }
#stage.fin .ic,#stage.fin .zh .zi,#stage.fin .dl,#stage.fin .c2,#stage.fin .fbot .fcc,#stage.fin .fbig,#stage.fin .wb,#stage.fin .wb b{ border-radius:4px; }
#stage.fin .ez,#stage.fin .ez.lv1,#stage.fin .ez.lv2,#stage.fin .ez.lv3,#stage.fin .fbot,#stage.fin .hdr .pill,#stage.fin .hdr .mn{
  box-shadow:0 8px 22px rgba(0,0,0,.12), 0 2px 5px rgba(0,0,0,.08) !important; }
#stage.fin .ez.lost{ box-shadow:0 0 0 2px #8A8F96, 0 8px 22px rgba(0,0,0,.12) !important; }
#stage.fin .hdr .pill{ border:0; text-align:center; -webkit-transition:background-color .28s; transition:background-color .28s; }
#stage.fin .hdr .pill.lv0{ background:linear-gradient(90deg, #6F747C, #7A7F87); }
#stage.fin .hdr .pill.lv1{ background:#E8A100; } #stage.fin .hdr .pill.lv2{ background:#E55A00; } #stage.fin .hdr .pill.lv3{ background:#D21018; }
#stage.fin .zst.lv0{ background-color:#6F747C; }
#stage.fin .fbig.lv1{ background:#45474C; }
#stage.fin .fbot .fc{ border-left-color:#D9DBDF; }
#stage.fin .wb i{ border-color:#4B5058; }
</style>
</head>
<body>
<div id="tb" role="toolbar" aria-label="시안 선택">
  <span class="ti">Smart AirGuard 구역별 관제 (최종)</span>
  <div class="g"><span class="gl">상황</span><select id="scn" aria-label="미리보기 상황">
    <option value="auto">자동 시연</option>
    <option value="n">정상</option>
    <option value="z0c">구역 1 주의</option>
    <option value="z1w">구역 2 경고</option>
    <option value="all3">두 구역 모두 위험</option>
    <option value="lost">구역 2 노드 통신 끊김</option>
  </select></div>
</div>
<div id="fit"><div id="stage"></div></div>
<script>window.DEF_D = 52;</script>
<script type="text/html" id="t52">
{{HDR}}
[[Z]]
<div class="ez" data-lv="z{i}" data-lost="z{i}" style="left:{at:24:693}px">
  <div class="rail" data-lv="z{i}"></div>
  <div class="zh"><span class="zi"><svg viewBox="0 0 24 24"><path fill-rule="evenodd" d="M3.3 21 a1 1 0 0 1 -1 -1 V11.6 a.9 .9 0 0 1 .9 -.9 H4 V5 a1 1 0 0 1 1 -1 h1.8 a1 1 0 0 1 1 1 V10.7 L12.6 7.2 a.6 .6 0 0 1 .9 .5 V10.7 L18.3 7.2 a.6 .6 0 0 1 .9 .5 V10.7 h1.6 a.9 .9 0 0 1 .9 .9 V20 a1 1 0 0 1 -1 1 Z M5.2 14.6 h2.2 v2.4 h-2.2 Z M9.3 14.6 h2.2 v2.4 h-2.2 Z M13.4 14.6 h2.2 v2.4 h-2.2 Z M17.5 14.6 h2.2 v2.4 h-2.2 Z"/><circle class="smk" cx="6.6" cy="2.1" r="1.2"/><circle class="smk" cx="8.9" cy="0.9" r=".75"/></svg></span><b>구역 {n}</b><span class="chip" data-node="z{i}"></span><span class="zw">{win} 노드 <span class="nid">{id}</span></span><div class="rg" data-rg="z{i}"></div></div>
  <div class="c cm" data-lv="z{i}.co" style="left:10px;top:62px;width:335px;height:230px">
    <div class="lab"><span class="ic" data-ix="co"></span>일산화탄소<small>(CO)</small></div>
    <div class="vv"><span class="num" data-v="z{i}.co" data-c="z{i}.co"></span><span class="u">ppm</span></div>
    <div class="dl" data-v="z{i}.codl" data-lv="z{i}.codlc"></div>
    <svg class="esp" data-sp="z{i}.co"></svg>
  </div>
  <div class="c cm tv" data-lv="z{i}.tvoc" style="left:345px;top:62px;width:336px;height:230px">
    <div class="lab"><span class="ic" data-ix="tvoc"></span>유기화합물<small>(TVOC)</small></div>
    <div class="vv"><span class="num" data-v="z{i}.tvoc" data-c="z{i}.tvoc"></span><span class="u">ppb</span></div>
    <div class="dl" data-v="z{i}.tvdl" data-lv="z{i}.tvdlc"></div>
    <svg class="esp" data-sp="z{i}.tvoc"></svg>
  </div>
  <div class="wrow">
    <svg class="win" data-win="{i}"></svg>
    <div class="wr"><div class="k">창문 개도율</div><div class="num"><span data-v="z{i}.win"></span><span class="u">%</span></div><div class="tg" data-v="z{i}.tgtx"></div><div class="st" data-v="z{i}.st"></div></div>
    <div class="wb" data-wb="{i}"><b></b><i></i></div>
  </div>
  <div class="zst" data-lv="z{i}"><span class="bi"><i class="s0"><svg viewBox="0 0 24 24"><path d="M12 3 L20 6 V11 C20 16 16.5 19.5 12 21 C7.5 19.5 4 16 4 11 V6 Z"/><path d="M8.5 12 L11 14.5 L15.5 9.5"/></svg></i><i class="s1"><svg viewBox="0 0 24 24"><path d="M12 3.5 L21.5 20 H2.5 Z"/><path d="M12 9.5 V14"/><circle cx="12" cy="17.2" r="0.6"/></svg></i></span><b data-v="z{i}.lvname"></b><em data-v="z{i}.lvnum"></em><span class="zc2" data-v="z{i}.shortcause"></span><span class="zd" data-v="z{i}.dur"></span></div>
</div>
[[/Z]]
<div class="fbot">
  <div class="fc" style="left:0;width:330px;border-left:0">
    <div class="lab"><span class="ic" data-ix="pm"></span>미세먼지<small>평균</small></div>
    <div class="fv"><span class="num" data-v="pm" data-c="pm"></span><span class="u">µg/m³</span></div>
    <div class="dl" data-v="pmdl" data-lv="pmdlc"></div><svg class="esp" data-sp="pm" data-spt="bar" style="width:286px"></svg>
  </div>
  <div class="fc" style="left:330px;width:210px">
    <div class="lab"><span class="ic" data-ix="temp"></span>온도</div>
    <div class="fv"><span class="num" data-v="temp"></span><span class="u">°C</span></div>
    <div class="dl" data-v="tempdl" data-lv="tempdlc"></div><svg class="esp" data-sp="temp" style="width:166px"></svg>
  </div>
  <div class="fc" style="left:540px;width:190px">
    <div class="lab"><span class="ic" data-ix="hum"></span>습도</div>
    <div class="fv"><span class="num" data-v="hum"></span><span class="u">%</span></div>
    <div class="dl" data-v="humdl" data-lv="humdlc"></div><svg class="esp" data-sp="hum" style="width:146px"></svg>
  </div>
  <div class="fc" style="left:730px;width:350px">
    <div class="lab"><span class="ic" data-ix="co2"></span>이산화탄소<small>(CO₂) 구역 평균</small></div><b class="c2 fcc" data-v="co2chip" data-lv="co2"></b>
    <div class="fv"><span class="num" data-v="co2" data-c="co2"></span><span class="u">ppm</span></div>
    <div class="dl" data-v="co2dl" data-lv="co2dlc"></div><svg class="esp" data-sp="co2" style="width:306px"></svg>
  </div>
  <div class="fc" style="left:1080px;width:294px">
    <div class="lab">환풍기 <span class="nid">0x201</span><span class="chip" data-node="fan"></span></div>
    <svg class="fan" data-fan="1"></svg>
    <div class="fr"><div class="fbig" data-v="fanbig" data-lv="fan"></div><div class="fst" data-v="fanst"></div></div>
  </div>
</script>
<script>
/* =====================================================================
   Smart AirGuard 구역별 관제 화면 - 시안 7종 공용 엔진 (ES5, 구형 안드로이드 호환)
   - DEMO : 미리보기에서는 가상 시나리오 재생, ESP32 AP(192.168.4.1)나 ?live=1 이면 /data 실데이터
   - ?d=1~7 로 시안 선택, ?kiosk=1 이면 위쪽 선택 막대 숨김 (태블릿 실전용)
   ===================================================================== */
var Q = location.search;
var DEMO = !(window.AG_LIVE || location.hostname === '192.168.4.1' || /[?&]live=1/.test(Q));
if (/[?&]demo=1/.test(Q)) DEMO = true;
var KIOSK = /[?&]kiosk=1/.test(Q) || !DEMO;
var SVGNS = 'http://www.w3.org/2000/svg';

var ZONES = [   // 구역 = 창문 = CAN 노드 (1:1). 구역을 늘리면 여기와 /data 키만 추가
  { nm:'구역 1', win:'창문 1', id:'0x101' },
  { nm:'구역 2', win:'창문 2', id:'0x102' }
];
var LV_NAME = ['정상', '주의', '경고', '위험'];
var LV_COL  = ['#00C29B', '#F4AC00', '#E55A00', '#D21018'];
var NORM = 'd';   // 정상 상태 색: g = 초록, d = 연한 짙은회색
var NORM_COL = { g:'#00C29B', d:'#4A5562' };
LV_COL[0] = NORM_COL[NORM];
function setNorm(v) {
  NORM = v; LV_COL[0] = (DES[CUR] && DES[CUR].norm) || NORM_COL[v];
  var st = document.getElementById('stage'); st.className = st.className.replace(/\s*nB/g, '') + (v === 'd' ? ' nB' : '');
  var b = document.querySelectorAll('#tb button[data-nv]');
  for (var i = 0; i < b.length; i++) b[i].className = b[i].getAttribute('data-nv') === v ? 'on' : '';
  try { localStorage.setItem('ag_norm', v); } catch (e) {}
}
var NUM_COL = ['', '#F4AC00', '#E05200', '#D21018'];   // 숫자 글자색: 밝은 바탕에서도 읽히게 노랑을 한 톤 짙게
var WIN_BY_LV = [0, 50, 75, 100];          // 등급별 창문 개방률 (팀 확정값)
var TH = { co:[10,30,50], tvoc:[220,660,2200], pm:[35,75,115], co2:[1000,1500,2000] };

function $(id) { return document.getElementById(id); }
function clamp(v, a, b) { return v < a ? a : (v > b ? b : v); }
function pad2(n) { return (n < 10 ? '0' : '') + n; }
function hhmmss(d) { return pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds()); }
function fmt(n) { n = Math.round(n); var s = String(Math.abs(n)).replace(/\B(?=(\d{3})+(?!\d))/g, ','); return (n < 0 ? '-' : '') + s; }
function mk(tag, at, parent) { var e = document.createElementNS(SVGNS, tag); for (var k in at) if (at.hasOwnProperty(k)) e.setAttribute(k, at[k]); if (parent) parent.appendChild(e); return e; }
function setCls(el, re, add) {
  var c = (el.getAttribute('class') || '').replace(re, ' ').replace(/\s+/g, ' ').replace(/^\s+|\s+$/g, '');
  c = c ? c + ' ' + add : add;
  if (el._cl !== c) { el.setAttribute('class', c); el._cl = c; }
}
var RE_LV = /(^|\s)lv\d(?=\s|$)/g, RE_ST = /(^|\s)(ok|bad|off)(?=\s|$)/g;

/* ---------------- 등급 계산 ---------------- */
function lvOf(k, v) { var t = TH[k]; return v < t[0] ? 0 : v < t[1] ? 1 : v < t[2] ? 2 : 3; }
function gN(k, v) {      // 기준값을 25 / 50 / 75 지점에 맞춘 0~100 (막대·게이지·지수 공용)
  var t = TH[k]; if (!(v > 0)) return 0;
  if (v < t[0]) return 25 * v / t[0];
  if (v < t[1]) return 25 + 25 * (v - t[0]) / (t[1] - t[0]);
  if (v < t[2]) return 50 + 25 * (v - t[1]) / (t[2] - t[1]);
  return Math.min(100, 75 + 25 * (v - t[2]) / (t[2] * 0.5));
}

/* ---------------- 상태 ---------------- */
var S = { z:[], pm:12, temp:23.6, hum:44, co2:520, fan:0, fanCmd:0, online:true, last:new Date(), age:{ main:30, fan:40 } };
for (var zi = 0; zi < ZONES.length; zi++) S.z.push({ co:1, tvoc:80, lv:0, win:0, tgt:0, cur:0, st:'닫힘', age:40, flt:0, pin:0, hist:[] });
function zIdx(i) { var z = S.z[i]; return Math.round(Math.max(gN('co', z.co), gN('tvoc', z.tvoc))); }
function avgIdx() { var s = 0; for (var i = 0; i < S.z.length; i++) s += zIdx(i); return Math.round(s / S.z.length); }
function avgLv() { var v = avgIdx(); return v < 25 ? 0 : v < 50 ? 1 : v < 75 ? 2 : 3; }
function allLv() { var m = Math.max(lvOf('pm', S.pm), lvOf('co2', S.co2)); for (var i = 0; i < S.z.length; i++) m = Math.max(m, S.z[i].lv); return m; }
function allIdx() { var m = Math.max(gN('pm', S.pm), gN('co2', S.co2)); for (var i = 0; i < S.z.length; i++) m = Math.max(m, zIdx(i)); return Math.round(m); }
function nodeSt(k) {
  var a = k === 'main' ? S.age.main : k === 'fan' ? S.age.fan : S.z[+k.charAt(1)].age;
  if (a < 0) return 'off';
  return (!S.online || a >= 1000) ? 'bad' : 'ok';
}

/* ---------------- 이벤트 기록 ---------------- */
var EV = [], evVer = 0;
function logEvent(m, lv) { EV.unshift({ t:hhmmss(new Date()), m:m, lv:lv }); if (EV.length > 8) EV.pop(); evVer++; }
var prevZ = [], prevN = [], prevFan = -1;
function checkEvents() {
  for (var i = 0; i < S.z.length; i++) {
    var lv = S.z[i].lv, p = prevZ[i];
    if (p === undefined || lv !== p) S.z[i].since = Date.now();
    if (p !== undefined && lv !== p) {
      if (lv === 0) logEvent(ZONES[i].nm + ' 정상 복귀, ' + ZONES[i].win + ' 닫음', 0);
      else if (lv > p) logEvent(ZONES[i].nm + ' ' + LV_NAME[lv] + ' 단계, ' + ZONES[i].win + ' ' + WIN_BY_LV[lv] + '% 개방', lv);
      else logEvent(ZONES[i].nm + ' ' + LV_NAME[lv] + ' 단계로 완화', lv);
    }
    prevZ[i] = lv;
    var ns = nodeSt('z' + i);
    if (prevN[i] !== undefined && ns !== prevN[i]) logEvent(ZONES[i].win + ' 노드 ' + (ns === 'ok' ? '다시 연결됨' : '응답 없음') + ' (' + ZONES[i].id + ')', ns === 'ok' ? 0 : 3);
    prevN[i] = ns;
  }
  var f = S.fanCmd > 50 ? 1 : 0;
  if (prevFan !== -1 && f !== prevFan) logEvent(f ? '환풍기 가동 (0x201)' : '환풍기 정지 (0x201)', f ? allLv() : 0);
  prevFan = f;
}

/* ---------------- 데모 시나리오 ---------------- */
var sim = { mode:'auto', ep:0, ph:'idle', t:0, calm:0, pinched:false };
var EPIS = [[1], [0], [0, 1]];                          // 구역 2 오염 → 구역 1 오염(끼임 연출) → 동시 오염
var FT = [[1, 80], [16, 330], [38, 950], [58, 2600]];   // 강제 상황별 CO, TVOC 목표
var SCN = { n:[0,0], z0c:[1,0], z1w:[0,2], all3:[3,3], lost:[1,0] };
function simStep(dt) {
  var src = [0, 0], i, z;
  if (sim.mode === 'auto') {
    sim.t += dt;
    if (sim.ph === 'idle' && sim.t > 6) { sim.ph = 'src'; sim.t = 0; sim.pinched = false; }
    else if (sim.ph === 'src' && sim.t > (EPIS[sim.ep].length > 1 ? 13 : 16)) { sim.ph = 'rec'; sim.t = 0; sim.calm = 0; }
    else if (sim.ph === 'rec') {
      var calm = allLv() === 0 && S.z[0].win < 1 && S.z[1].win < 1 && S.fan < 1;
      sim.calm = calm ? sim.calm + dt : 0;
      if (sim.calm > 4) { sim.ph = 'idle'; sim.t = 0; sim.ep = (sim.ep + 1) % EPIS.length; }
    }
    if (sim.ph === 'src') for (i = 0; i < EPIS[sim.ep].length; i++) src[EPIS[sim.ep][i]] = 1;
    for (i = 0; i < S.z.length; i++) {
      z = S.z[i]; var o = S.z[1 - i];
      var kv = 0.014 + 0.08 * z.win / 100 + 0.035 * S.fan / 100;
      if (src[i]) { z.co += 3.9 * dt; z.tvoc += 155 * dt; }
      z.co -= (z.co - 1) * kv * dt; z.tvoc -= (z.tvoc - 80) * kv * dt;
      z.co += (o.co - z.co) * 0.012 * dt; z.tvoc += (o.tvoc - z.tvoc) * 0.012 * dt;
      z.co = Math.max(0, z.co + (Math.random() - 0.5) * 0.2); z.tvoc = Math.max(0, z.tvoc + (Math.random() - 0.5) * 8);
    }
    var any = src[0] || src[1], kva = 0.014 + 0.04 * (S.z[0].win + S.z[1].win) / 200 + 0.03 * S.fan / 100;
    if (any) { S.pm += 2.6 * dt; S.co2 += 38 * dt; S.temp += 0.02 * dt; S.hum += 0.1 * dt; }
    S.pm -= (S.pm - 11) * kva * dt; S.co2 -= (S.co2 - 480) * kva * dt;
    S.temp -= (S.temp - 23.6) * 0.02 * dt; S.hum -= (S.hum - 44) * 0.03 * dt;
  } else {
    var tg = SCN[sim.mode], a = Math.min(1, 1.6 * dt), mx = Math.max(tg[0], tg[1]);
    for (i = 0; i < S.z.length; i++) {
      z = S.z[i];
      z.co += (FT[tg[i]][0] - z.co) * a + (Math.random() - 0.5) * 0.2;
      z.tvoc += (FT[tg[i]][1] - z.tvoc) * a + (Math.random() - 0.5) * 8;
    }
    S.pm += ((mx === 3 ? 92 : mx > 0 ? 22 : 11) - S.pm) * a;
    S.co2 += ((mx === 3 ? 1620 : mx > 0 ? 760 : 490) - S.co2) * a;
    S.temp += (23.6 - S.temp) * a; S.hum += (44 - S.hum) * a;
  }
  S.pm += (Math.random() - 0.5) * 0.6; S.co2 += (Math.random() - 0.5) * 5;
  for (i = 0; i < S.z.length; i++) {             // 등급 (내려갈 때 8% 여유로 들쭉날쭉 방지)
    z = S.z[i];
    var up = Math.max(lvOf('co', z.co), lvOf('tvoc', z.tvoc));
    var dn = Math.max(lvOf('co', z.co * 1.08), lvOf('tvoc', z.tvoc * 1.08));
    if (up > z.lv) z.lv = up; else if (dn < z.lv) z.lv = dn;
    z.age = 20 + Math.round(Math.random() * 160);
  }
  S.age.main = 20 + Math.round(Math.random() * 120); S.age.fan = 20 + Math.round(Math.random() * 160);
  if (sim.mode === 'lost') S.z[1].age = 1800;
  S.online = true; S.last = new Date();
  actuate(dt);
}
function actuate(dt) {
  for (var i = 0; i < S.z.length; i++) {
    var z = S.z[i], nm = ZONES[i].win;
    if (nodeSt('z' + i) === 'bad') { z.st = '응답 없음'; z.cur = 0; continue; }
    z.tgt = WIN_BY_LV[z.lv];
    if (z.pin > 0) {
      z.pin -= dt; z.cur = z.pin > 2.4 ? 1.62 : 0; z.st = '끼임 감지, 후퇴';
      if (z.pin <= 0) logEvent(nm + ' 다시 여는 중', z.lv);
      continue;
    }
    var d = z.tgt - z.win, step = 12 * dt;
    if (sim.mode === 'auto' && i === 0 && !sim.pinched && d > 0 && z.win > 38) {
      sim.pinched = true; z.pin = 3; z.win -= 4; logEvent(nm + ' 끼임 감지, 정지 후 후퇴', 3); continue;
    }
    if (Math.abs(d) <= step) { z.win = z.tgt; z.cur = z.win > 0 ? 0.08 : 0; z.st = z.win > 0 ? '열림 유지' : '닫힘'; }
    else { z.win += d > 0 ? step : -step; z.cur = 0.62 + Math.random() * 0.2; z.st = d > 0 ? '여는 중' : '닫는 중'; }
  }
  S.fanCmd = allLv() >= 1 ? 100 : 0;            // 주의부터 ON, 정상이면 OFF
  var fd = S.fanCmd - S.fan; S.fan += Math.abs(fd) < 60 * dt ? fd : (fd > 0 ? 60 * dt : -60 * dt);
}

/* ---------------- 실데이터 (ESP32 /data) ----------------
   구역별 키: z1co, z1tvoc, z1lv / z2co, z2tvoc, z2lv  (없으면 co, tvoc 공용값과 자체 계산으로 대체)
   co2 = 마스터가 계산한 두 구역 ENS160 eCO2 평균 (참고: z1co2, z2co2 도 같이 옴)
   기존 키 그대로: win1, win2, cur1, cur2, flt1, flt2, cmd_win1/cmd_win2 (또는 cmd_win), fan, cmd_fan,
                   dust, co2, temp, hum, nodes[메인, 창문1, 창문2, 환풍기] (ms 전 수신, 미연결 -1)          */
function nv(v) { return (typeof v === 'number' && isFinite(v)) ? v : null; }
function pick() { for (var i = 0; i < arguments.length; i++) { var v = nv(arguments[i]); if (v !== null) return v; } return 0; }
var FLT = ['', '엔코더 이상', '걸림 감지', '범위 이탈', '드라이버 고장', '끼임 감지'];
function applyLive(d) {
  for (var i = 0; i < S.z.length; i++) {
    var z = S.z[i], n = i + 1, prev = z.win;
    z.co = pick(d['z' + n + 'co'], d.co); z.tvoc = pick(d['z' + n + 'tvoc'], d.tvoc);
    var lv = nv(d['z' + n + 'lv']);
    z.lv = lv !== null ? clamp(lv, 0, 3) : Math.max(lvOf('co', z.co), lvOf('tvoc', z.tvoc));
    z.win = pick(d['win' + n]); z.cur = pick(d['cur' + n]); z.flt = pick(d['flt' + n]);
    z.cmd = pick(d['cmd_win' + n], d.cmd_win, WIN_BY_LV[z.lv]);    // 마스터가 보낸 목표
    var ntg = nv(d['w' + n + 'tg']);                                  // 노드가 실제로 가는 목표 (닫기 대기 중엔 이전 값)
    z.tgt = (ntg !== null && ntg >= 0) ? ntg : z.cmd;
    z.age = d.nodes ? pick(d.nodes[n]) : -1;
    // 남은 시간: ms -> 이 화면 시계 기준 "끝나는 시각" 으로 바꿔 두고, 200ms 마다 다시 계산 (1초 폴링 사이에도 초가 줄어듦)
    var now = Date.now(), dn = pick(d['w' + n + 'dn']), cw = pick(d['w' + n + 'cw']), pr = pick(d['w' + n + 'pr']);
    var wp = nv(d['w' + n + 'wp']);
    z.dnUntil = dn > 0 ? now + dn : 0;                                // 마스터 등급 하향 확인
    z.cwUntil = (cw > 0 && wp !== null && wp <= 100) ? now + cw : 0;  // 노드 닫기 대기 (5초)
    z.cwPct = wp !== null && wp <= 100 ? wp : -1;
    z.pf = pick(d['w' + n + 'pf']); z.ps = pick(d['w' + n + 'ps']);
    z.pn = pick(d['w' + n + 'pn']); z.pm = pick(d['w' + n + 'pm']);
    z.prUntil = pr > 0 ? now + pr : 0;                                // 끼임 재시도
    z.pin = (z.flt === 5 || z.flt === 2 || (z.pf & 2)) ? 1 : 0;      // 끼임/걸림: 창문 테두리 빨강 + 안내 문구
    if (d.nodes && (z.age < 0 || z.age >= 1000)) z.st = z.age < 0 ? '미연결' : '응답 없음';
    else if (z.flt > 0) z.st = '정지: ' + (FLT[z.flt] || '고장');
    else if (Math.abs(z.win - z.tgt) > 1) z.st = z.win < z.tgt ? '여는 중' : '닫는 중';
    else z.st = z.win > prev ? '여는 중' : z.win < prev ? '닫는 중' : (z.win > 0 ? '열림 유지' : '닫힘');
  }
  S.pm = pick(d.dust, d.pm); S.co2 = pick(d.co2); S.temp = pick(d.temp); S.hum = pick(d.hum);
  S.fan = pick(d.fan); S.fanCmd = pick(d.cmd_fan, d.fan);
  S.age.main = d.nodes ? pick(d.nodes[0]) : pick(d.age);
  S.age.fan = d.nodes && d.nodes.length > 3 ? pick(d.nodes[3]) : -1;
  S.last = new Date();
}
var busy = false, busyAt = 0, lastOk = 0;
function poll() {
  var t = new Date().getTime();
  if (busy && t - busyAt < 3000) return;
  busy = true; busyAt = t;
  var x = new XMLHttpRequest();
  x.onreadystatechange = function () {
    if (x.readyState !== 4) return;
    busy = false;
    if (x.status === 200) { try { applyLive(JSON.parse(x.responseText)); lastOk = new Date().getTime(); } catch (e) {} }
  };
  x.open('GET', '/data?t=' + t, true); x.send(null);
}

/* ---------------- 남은 시간 (히스테리시스 / 끼임 재시도) ---------------- */
var PSRC = ['', '전류', '전압', '걸림'];
function left(t) { return t ? Math.max(0, Math.ceil((t - Date.now()) / 1000)) : 0; }
// 창문 상태에 붙는 안내: 끼임 > 닫기 대기 > 등급 하향 확인 순.  없으면 ''
function winNote(z) {
  var s;
  if (z.pf & 2) {
    var src = PSRC[z.ps] ? '(' + PSRC[z.ps] + ')' : '';
    if (z.pf & 8) return '끼임' + src + ' · 뒤로 물러나는 중';
    if (z.pf & 4) return '끼임 정지 · 재시도 ' + z.pm + '번 끝, 확인 필요';
    s = left(z.prUntil);
    return '끼임' + src + ' · ' + (s > 0 ? s + '초 후 ' : '곧 ') + '재시도 ' + (z.pn + 1) + '/' + z.pm;
  }
  if (z.cwUntil) { s = left(z.cwUntil); return s > 0 ? '닫기 대기 ' + s + '초' : '곧 닫힘'; }
  if (z.dnUntil) { s = left(z.dnUntil); return '등급 하향 확인 ' + (s > 0 ? s + '초' : '곧'); }
  return '';
}

/* ---------------- 화면에 쓰이는 값 ---------------- */
var DAYS = ['일', '월', '화', '수', '목', '금', '토'];
function zMsg(i) {
  var z = S.z[i], w = ZONES[i].win;
  if (nodeSt('z' + i) === 'bad') return w + ' 노드 응답 없음, 상태 확인 필요';
  if (z.pin > 0) return w + ' ' + (winNote(z) || '끼임 감지, 멈추고 뒤로 물러남');
  if (winNote(z)) return w + ' ' + winNote(z) + ' (지금 ' + Math.round(z.win) + '%)';
  if (z.lv === 0) return z.win > 1 ? '정상 복귀, ' + w + ' 닫는 중' : '공기 양호, ' + w + ' 닫힘';
  var moving = Math.abs(z.win - z.tgt) > 1;
  return LV_NAME[z.lv] + ' 단계, ' + w + ' ' + z.tgt + '% ' + (moving ? '여는 중' : '개방 완료');
}
function zCause(i) {
  var z = S.z[i], a = lvOf('co', z.co), b = lvOf('tvoc', z.tvoc);
  if (a === 0 && b === 0) return 'CO ' + fmt(z.co) + ' ppm, TVOC ' + fmt(z.tvoc) + ' ppb 모두 기준 아래';
  if (a >= b) return 'CO ' + fmt(z.co) + ' ppm, ' + LV_NAME[a] + ' 기준 ' + TH.co[a - 1] + ' ppm 이상';
  return 'TVOC ' + fmt(z.tvoc) + ' ppb, ' + LV_NAME[b] + ' 기준 ' + fmt(TH.tvoc[b - 1]) + ' ppb 이상';
}
function V(k) {
  var m = /^z(\d)\.(\w+)$/.exec(k);
  if (m) {
    var i = +m[1], z = S.z[i], f = m[2];
    var lost = nodeSt('z' + i) === 'bad';
    if (!lost) z.lastOk = new Date();
    if (lost) switch (f) {   // 노드가 끊기면 그 노드의 센서값도 믿을 수 없음: 마지막 값을 정상처럼 보여주지 않음
      case 'co': case 'tvoc': case 'win': return '--';
      case 'codl': case 'tvdl': return '';
      case 'lvname': return '통신 끊김';
      case 'lvnum': return '';
      case 'shortcause': return '센서값과 창문 상태를 알 수 없음';
      case 'dur': return '마지막 수신 ' + (z.lastOk ? hhmmss(z.lastOk) : '-');
      case 'tgtx': return '';
    }
    switch (f) {
      case 'co': return fmt(z.co);
      case 'tvoc': return fmt(z.tvoc);
      case 'win': return String(Math.round(z.win));
      case 'tgt': return String(z.tgt);
      case 'tgtx': return z.cwUntil && z.cwPct >= 0 ? '목표 ' + z.tgt + '% → ' + z.cwPct + '% (' + left(z.cwUntil) + '초)'
                        : '목표 ' + z.tgt + '%';
      case 'cmd': return '창문 목표 ' + z.tgt + '%';
      case 'cur': return z.cur.toFixed(2);
      case 'st': return winNote(z) || z.st;
      case 'lvname': return LV_NAME[z.lv];
      case 'lvnum': return z.lv + '단계';
      case 'lvi': return String(z.lv);
      case 'idx': return String(zIdx(i));
      case 'msg': return zMsg(i);
      case 'cause': return zCause(i);
      case 'dur': var s = Math.max(0, Math.floor((Date.now() - (z.since || Date.now())) / 1000)), mm = Math.floor(s / 60), ss = s % 60;
        return (z.lv > 0 ? LV_NAME[z.lv] + ' 진입 후 ' : '정상 유지 ') + mm + ':' + pad2(ss);
      case 'codl': return dTxt('z' + i + '.co');
      case 'tvdl': return dTxt('z' + i + '.tv');
      case 'cotr': return trendTxt(z.hco, z.co, 0.5);
      case 'tvtr': return trendTxt(z.htv, z.tvoc, 12);
      case 'shortcause': var wn = winNote(z);
        if (wn) return wn;
        return z.lv > 0 ? causeShort(i) + ' 기준 초과' : '모든 값 기준 아래';
      case 'a1': return nodeSt('z' + i) === 'bad' ? ZONES[i].win + ' 노드 점검' : ZONES[i].win + ' ' + z.tgt + '% 개방';
      case 'a1s': return nodeSt('z' + i) === 'bad' ? '확인 필요' : z.lv === 0 ? '대기' : (Math.abs(z.win - z.tgt) > 1 ? '진행 중' : '완료');
      case 'a2s': return S.fan > 90 ? '가동 중' : (S.fanCmd > 50 ? '시작 중' : '대기');
      case 'a3s': return z.lv >= 3 ? '울림' : '위험 단계에서';
    }
    return '';
  }
  var lv = allLv(), d;
  switch (k) {
    case 'pm': return fmt(S.pm);
    case 'co2': return fmt(S.co2);
    case 'temp': return S.temp.toFixed(1);
    case 'hum': return String(Math.round(S.hum));
    case 'lvname': return LV_NAME[lv];
    case 'lvnum': return lv + '단계';
    case 'idx': return String(allIdx());
    case 'fanbig': return S.fanCmd > 50 ? '가동' : '정지';
    case 'fanst': return S.fanCmd > 50 ? (S.fan > 90 ? '최대 출력 배기' : '가동 시작') : (S.fan > 1 ? '멈추는 중' : '주의 단계부터 가동');
    case 'fancmd': return S.fanCmd > 50 ? '환풍기 ON' : '환풍기 OFF';
    case 'co2chip': return ['환기 양호', '환기 권장', '환기 필요', '즉시 환기'][lvOf('co2', S.co2)];
    case 'pmdl': case 'co2dl': case 'tempdl': case 'humdl': return dTxt(k.replace('dl', ''));
    case 'avgidx': return String(avgIdx());
    case 'avglvname': return LV_NAME[avgLv()];
    case 'avglvnum': return avgLv() + '단계';
    case 'bsum':
      var bl = []; for (var bi = 0; bi < S.z.length; bi++) if (S.z[bi].lv > 0) bl.push(ZONES[bi].nm + ' ' + LV_NAME[S.z[bi].lv] + ' (' + causeShort(bi) + ')');
      if (lvOf('pm', S.pm) > 0) bl.push('미세먼지 ' + LV_NAME[lvOf('pm', S.pm)]); if (lvOf('co2', S.co2) > 0) bl.push('이산화탄소 ' + LV_NAME[lvOf('co2', S.co2)]);
      return bl.length ? bl.join('   ') : '모든 구역 정상, 창문 닫힘';
    case 'buzz': return lv >= 3 ? '3회 울림' : '대기';
    case 'clock': d = new Date(); return pad2(d.getHours()) + ':' + pad2(d.getMinutes());
    case 'date': d = new Date(); return (d.getMonth() + 1) + '월 ' + d.getDate() + '일 ' + DAYS[d.getDay()] + '요일';
    case 'sys': return S.online ? '시스템 작동 중' : '시스템 작동 이상';
    case 'conn': return S.online ? '센서 데이터 수신 중' : '연결 끊김';
    case 'upd': return '마지막 갱신 ' + hhmmss(S.last);
  }
  return '';
}
function LK(k) {        // 색을 정하는 등급
  var m;
  if (k === 'all') return allLv();
  if (k === 'avg') return avgLv();
  if (k === 'link') return S.online ? 0 : 3;
  if (k === 'fan') return S.fanCmd > 50 ? 1 : 0;
  if (k === 'fanrun') return S.fan > 10 ? 1 : 0;
  if ((m = /^(z\d\.co|z\d\.tv|pm|co2|temp|hum)dlc$/.exec(k))) { var da = dAbs(m[1]), ep = DEPS[m[1].replace(/^z\d\./, '')]; return da > ep ? 1 : da < -ep ? 2 : 0; }
  if ((m = /^z(\d)\.open$/.exec(k))) return S.z[+m[1]].win > 3 ? 1 : 0;
  if (k === 'pm' || k === 'co2') return lvOf(k, S[k]);
  if ((m = /^z(\d)$/.exec(k))) return S.z[+m[1]].lv;
  if ((m = /^zf(\d)$/.exec(k))) return nodeSt('z' + m[1]) === 'bad' ? 3 : S.z[+m[1]].lv;
  if ((m = /^z(\d)\.(co|tvoc)$/.exec(k))) return lvOf(m[2], S.z[+m[1]][m[2]]);
  if ((m = /^z(\d)\.a(\d)d$/.exec(k))) {
    var z = S.z[+m[1]];
    if (m[2] === '1') return z.lv > 0 && Math.abs(z.win - z.tgt) <= 1 ? 1 : 0;
    if (m[2] === '2') return S.fan > 90 ? 1 : 0;
    return z.lv >= 3 ? 1 : 0;
  }
  return 0;
}
function PK(k) {        // 막대 길이 0~100
  var m;
  if (k === 'avg') return avgIdx();
  if (k === 'all') return allIdx();
  if (k === 'pm' || k === 'co2') return gN(k, S[k]);
  if ((m = /^z(\d)$/.exec(k))) return zIdx(+m[1]);
  if ((m = /^z(\d)\.(co|tvoc)$/.exec(k))) return gN(m[2], S.z[+m[1]][m[2]]);
  return 0;
}

/* ---------------- 아이콘 ---------------- */
var CLOUD = 'M13 33 C7.5 33 4 29.2 4 24.8 C4 20.4 7.4 17.2 11.6 17 C12.8 11.4 17.4 7.6 23 7.6 C29 7.6 33.6 11.8 34.6 17.4 C39.6 17.6 43.4 21 43.4 25.4 C43.4 29.8 40 33 35.6 33 Z';
var ICON = {
  co:  '<svg viewBox="0 0 48 48"><circle cx="24" cy="24" r="19" fill="none" stroke="#EB8500" stroke-width="3.2"/><text x="24" y="29.2" text-anchor="middle" fill="#EB8500" font-size="14.5" font-weight="700">CO</text></svg>',
  tvoc:'<svg viewBox="0 0 48 48"><path d="' + CLOUD + '" fill="none" stroke="#8A3FE8" stroke-width="3.2" stroke-linejoin="round"/><text x="23.7" y="27.6" text-anchor="middle" fill="#8A3FE8" font-size="9.6" font-weight="800">TVOC</text><path d="M14 40 q3 -2.4 6 0 t6 0" fill="none" stroke="#8A3FE8" stroke-width="2" stroke-linecap="round" opacity=".75"/></svg>',
  pm:  '<svg viewBox="0 0 48 48"><path d="' + CLOUD + '" fill="none" stroke="#3E4751" stroke-width="3" stroke-linejoin="round" transform="translate(0 4)"/><g fill="#3E4751"><circle cx="15" cy="28" r="2.2"/><circle cx="22" cy="23" r="1.6"/><circle cx="27" cy="30" r="2"/><circle cx="32" cy="24" r="1.5"/><circle cx="20" cy="31" r="1.3"/></g></svg>',
  temp:'<svg viewBox="0 0 30 40"><path d="M11 24 V7.5 a4 4 0 0 1 8 0 V24 a8 8 0 1 1 -8 0 Z" fill="none" stroke="#3E4751" stroke-width="2" stroke-linejoin="round"/><line x1="15" y1="11.5" x2="15" y2="27" stroke="#3E4751" stroke-width="3.6" stroke-linecap="round"/><circle cx="15" cy="30.5" r="4.8" fill="#3E4751"/></svg>',
  hum: '<svg viewBox="0 0 30 40"><path d="M15 4 C15 4 5 16.5 5 24.5 a10 10 0 0 0 20 0 C25 16.5 15 4 15 4 Z" fill="none" stroke="#3E4751" stroke-width="2.4" stroke-linejoin="round"/><path d="M10.5 25 a4.8 4.8 0 0 0 4.2 4.6" fill="none" stroke="#3E4751" stroke-width="2.2" stroke-linecap="round"/></svg>',
  co2: '<svg viewBox="0 0 48 48"><circle cx="24" cy="24" r="19" fill="none" stroke="#1FA855" stroke-width="3.2"/><text x="21.5" y="28.6" text-anchor="middle" fill="#1FA855" font-size="12.5" font-weight="800">CO</text><text x="33.6" y="31.6" text-anchor="middle" fill="#1FA855" font-size="8" font-weight="800">2</text></svg>'
};

var ICONM = {   // 고성능 HMI용 단색 아이콘 (글자색을 따라감 → 경보 시 칸 색과 함께 반전)
  factory:'<svg viewBox="0 0 24 24"><path d="M3 21 V11 L8 14 V11 L13 14 V8 H16 V4 H19 V21 Z" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"/></svg>',
  co:  '<svg viewBox="0 0 48 48"><circle cx="24" cy="24" r="19" fill="none" stroke="currentColor" stroke-width="3.6"/><text x="24" y="29.4" text-anchor="middle" fill="currentColor" font-size="15" font-weight="800">CO</text></svg>',
  tvoc:'<svg viewBox="0 0 48 48"><path d="' + CLOUD + '" fill="none" stroke="currentColor" stroke-width="3.6" stroke-linejoin="round"/><text x="23.7" y="27.6" text-anchor="middle" fill="currentColor" font-size="9.8" font-weight="800">TVOC</text><path d="M14 40 q3 -2.4 6 0 t6 0" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round"/></svg>',
  pm:  '<svg viewBox="0 0 48 48"><path d="' + CLOUD + '" fill="none" stroke="currentColor" stroke-width="3.4" stroke-linejoin="round" transform="translate(0 4)"/><g fill="currentColor"><circle cx="15" cy="28" r="2.4"/><circle cx="22" cy="23" r="1.8"/><circle cx="27" cy="30" r="2.2"/><circle cx="32" cy="24" r="1.7"/><circle cx="20" cy="31" r="1.5"/></g></svg>',
  temp:'<svg viewBox="0 0 30 40"><path d="M11 24 V7.5 a4 4 0 0 1 8 0 V24 a8 8 0 1 1 -8 0 Z" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linejoin="round"/><line x1="15" y1="11.5" x2="15" y2="27" stroke="currentColor" stroke-width="3.6" stroke-linecap="round"/><circle cx="15" cy="30.5" r="4.8" fill="currentColor"/></svg>',
  hum: '<svg viewBox="0 0 30 40"><path d="M15 4 C15 4 5 16.5 5 24.5 a10 10 0 0 0 20 0 C25 16.5 15 4 15 4 Z" fill="none" stroke="currentColor" stroke-width="2.8" stroke-linejoin="round"/><path d="M10.5 25 a4.8 4.8 0 0 0 4.2 4.6" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round"/></svg>',
  co2: '<svg viewBox="0 0 48 48"><circle cx="24" cy="24" r="19" fill="none" stroke="currentColor" stroke-width="3.6"/><text x="21.5" y="28.8" text-anchor="middle" fill="currentColor" font-size="13" font-weight="800">CO</text><text x="33.8" y="31.8" text-anchor="middle" fill="currentColor" font-size="8.5" font-weight="800">2</text></svg>'
};

/* ---------------- 그림 부품: 게이지 ---------------- */
function pt(cx, cy, r, deg) { var a = deg * Math.PI / 180; return (cx + r * Math.sin(a)).toFixed(2) + ' ' + (cy - r * Math.cos(a)).toFixed(2); }
function arc(cx, cy, r, a0, a1) { return 'M ' + pt(cx, cy, r, a0) + ' A ' + r + ' ' + r + ' 0 ' + ((a1 - a0) > 180 ? 1 : 0) + ' 1 ' + pt(cx, cy, r, a1); }
var GAUGES = [], WINS = [], FANS = [], SPARKS = [], PWINS = [], UID = 0;
function buildGauge(svg, key, sty) {
  var hero = sty === 'hero';
  svg.setAttribute('viewBox', '0 0 214 200');
  for (var i = 0; i < 4; i++)
    mk('path', { d:arc(107, 110, 84, -120 + i * 60 + 1.5, -120 + (i + 1) * 60 - 1.5), stroke:hero ? '#FFFFFF' : LV_COL[i], 'stroke-width':18, fill:'none', opacity:hero ? 0.28 : 0.2 }, svg);
  var f = mk('path', { d:arc(107, 110, 84, -120, -119), stroke:hero ? '#FFFFFF' : LV_COL[0], 'stroke-width':18, fill:'none', 'stroke-linecap':'round' }, svg);
  GAUGES.push({ f:f, key:key, hero:hero, disp:1 });
}
function animGauge(g) {
  var t = clamp(PK(g.key), 1, 100);
  g.disp += (t - g.disp) * 0.18; if (Math.abs(t - g.disp) < 0.05) g.disp = t;
  g.f.setAttribute('d', arc(107, 110, 84, -120, -120 + 240 * g.disp / 100));
  if (!g.hero) g.f.setAttribute('stroke', LV_COL[Math.min(3, Math.floor(g.disp / 25))]);
}

/* ---------------- 그림 부품: 랙-피니언 미닫이 창문 (정면) ---------------- */
var WPAL = {
  glass:{ fr:'#EEF2F6', frs:'#97A4B2', sky:'#DCEBF7', pn:'#C7D3DF', pns:'#55616E', gl:'#F4F8FB', rk:'#55616E', wd:'#6F86A0' },
  dark: { fr:'#2B3138', frs:'#8E98A3', sky:'#1A1F24', pn:'#3A424B', pns:'#B4BCC4', gl:'#5F6A75', rk:'#B4BCC4', wd:'#9AA5B1' },
  hmi:  { fr:'#E3E6E9', frs:'#4E5862', sky:'#FFFFFF', pn:'#C3CAD1', pns:'#2E3842', gl:'#F0F2F4', rk:'#2E3842', wd:'#4E5862' }
};
/* 창문 (기어 없는 고급형): 얇은 테두리, 유리 그라데이션, 창틀 안쪽 단차, 창턱 */
function dSeries(key) {
  var m = /^z(\d)\.(co|tv)$/.exec(key);
  if (m) { var z = S.z[+m[1]]; return m[2] === 'co' ? [z.hco, z.co] : [z.htv, z.tvoc]; }
  return [S.hs ? S.hs[key] : null, S[key]];
}
function dPct(key) {
  var d = dSeries(key), h = d[0]; if (!h || h.length < 2) return 0;
  var old = h[Math.max(0, h.length - 61)], base = Math.max(Math.abs(old), key === 'temp' ? 1 : key === 'hum' ? 1 : 0.5);
  return (d[1] - old) / base * 100;
}
var DEPS = { co:0.3, tv:10, pm:1, co2:8, temp:0.1, hum:0.5 };
var DUNIT = { co:' ppm', tv:' ppb', pm:'', co2:'', temp:'°C', hum:'%' };
function dAbs(key) { var d = dSeries(key), h = d[0]; if (!h || h.length < 2) return 0; return d[1] - h[Math.max(0, h.length - 11)]; }   // 최근 10초 변화: 지금 오르는 중이면 +, 내리는 중이면 -
function dTxt(key) {
  var a = dAbs(key), b = key.replace(/^z\d\./, ''), ep = DEPS[b], s = a > ep ? '+' : a < -ep ? '-' : '±', v = Math.abs(a);
  if (s === '±') v = 0;
  return s + (b === 'temp' ? v.toFixed(1) : fmt(v)) + DUNIT[b];
}
function buildRing(box, key) {
  box.innerHTML = '<svg viewBox="0 0 48 48"><circle cx="24" cy="24" r="20" fill="none" stroke="#ECECEE" stroke-width="4"/><circle class="rgp" cx="24" cy="24" r="20" fill="none" stroke-width="4" stroke-linecap="round" stroke-dasharray="125.7" stroke-dashoffset="125.7" transform="rotate(-90 24 24)"/></svg><b></b>';
  return { key:key, p:box.querySelector('.rgp'), t:box.getElementsByTagName('b')[0], last:-1 };
}
function drawRing(o) {
  var v = PK(o.key), lv = LK(o.key);
  if (Math.round(v) === o.last && o.lv === lv) return;
  o.last = Math.round(v); o.lv = lv;
  o.p.setAttribute('stroke-dashoffset', (125.7 * (1 - Math.max(0.02, v / 100))).toFixed(1));
  o.p.setAttribute('stroke', lv > 0 ? LV_COL[lv] : '#4B5058');
  o.t.textContent = o.last;
}
/* 창문 (J형): 얇은 선, 둥근 모서리, 단색 유리, 그라데이션 바람 */
function buildWinJ(svg, zi, gray) {
  var id = 'wj' + (++UID), k;
  svg.setAttribute('viewBox', '0 0 200 120');
  var defs = mk('defs', {}, svg);
  var g = mk('linearGradient', { id:id + 'w', x1:0, y1:0, x2:1, y2:0 }, defs);
  mk('stop', { offset:0, 'stop-color':'#C4C4C4' }, g); mk('stop', { offset:1, 'stop-color':'#7A7A7A' }, g);
  var cp = mk('clipPath', { id:id + 'c' }, defs), clip = mk('rect', { x:12, y:12, width:0, height:94 }, cp);
  mk('rect', { x:4, y:4, width:192, height:110, rx:4, fill:'#EFEFF2', stroke:'#7E8892', 'stroke-width':1 }, svg);
  mk('rect', { x:8, y:8, width:184, height:102, rx:3, fill:gray ? '#F7F7F7' : '#F2F6FB', stroke:'#A9B2BB', 'stroke-width':0.8 }, svg);
  function pane(parent, x, w, handle) {
    var fr = mk('rect', { x:x, y:12, width:w, height:94, rx:2.5, fill:'#EFEFF2', stroke:'#6E7882', 'stroke-width':1.2 }, parent);
    mk('rect', { x:x + 3, y:15, width:w - 6, height:88, rx:1.5, fill:gray ? '#EEEEEE' : '#E4EEF9', stroke:'#9AA3AC', 'stroke-width':0.8 }, parent);
    mk('path', { d:'M' + (x + 16) + ' 40 L' + (x + 36) + ' 26', stroke:'#FFFFFF', 'stroke-width':5, 'stroke-linecap':'round', opacity:0.9 }, parent);
    mk('path', { d:'M' + (x + 16) + ' 54 L' + (x + 26) + ' 47', stroke:'#FFFFFF', 'stroke-width':5, 'stroke-linecap':'round', opacity:0.9 }, parent);
    if (handle) mk('rect', { x:x + w - 5.5, y:49, width:5, height:20, rx:2.5, fill:'#6E7882' }, parent);
    return fr;
  }
  pane(svg, 96, 92, false);
  var wl = mk('g', { 'clip-path':'url(#' + id + 'c)' }, svg), wind = [];
  var shapes = ['M0 0 H-46 a8 8 0 1 1 8 -8', 'M0 0 H-28 a6 6 0 1 1 6 -6', 'M0 0 H-28 a6 6 0 1 1 6 -6'], lanes = [62, 38, 84];   // 큰 것 1개, 작은 것 2개
  for (k = 0; k < 3; k++)
    wind.push({ p:mk('path', { d:shapes[k], fill:'none', stroke:'url(#' + id + 'w)', 'stroke-width':4, 'stroke-linecap':'round', opacity:0 }, wl), t:k * 0.33, lane:lanes[k], sp:0.42 + (k % 2) * 0.12 });
  var sash = mk('g', {}, svg);
  mk('rect', { x:14, y:16, width:92, height:92, rx:2.5, fill:'rgba(28,28,30,.08)' }, sash);
  var sr = pane(sash, 12, 92, true);
  WINS.push({ zi:zi, clip:clip, sash:sash, sr:sr, pin:null, wind:wind, P:{ pns:'#6E7882' }, sw:2, sc0:'#6E7882', jw:1 });
}
function buildWinFine(svg, zi) {
  var id = 'wf' + (++UID), k;
  svg.setAttribute('viewBox', '0 0 200 120');
  var defs = mk('defs', {}, svg);
  var g1 = mk('linearGradient', { id:id + 'g', x1:0, y1:0, x2:1, y2:1 }, defs);
  mk('stop', { offset:0, 'stop-color':'#E6EEF5' }, g1); mk('stop', { offset:0.55, 'stop-color':'#C9D6E2' }, g1); mk('stop', { offset:1, 'stop-color':'#B3C3D2' }, g1);
  var g2 = mk('linearGradient', { id:id + 'f', x1:0, y1:0, x2:0, y2:1 }, defs);
  mk('stop', { offset:0, 'stop-color':'#F7F8FA' }, g2); mk('stop', { offset:1, 'stop-color':'#DCE1E6' }, g2);
  var g3 = mk('linearGradient', { id:id + 's', x1:0, y1:0, x2:0, y2:1 }, defs);
  mk('stop', { offset:0, 'stop-color':'#EEF6FC' }, g3); mk('stop', { offset:1, 'stop-color':'#D6E7F3' }, g3);
  var cp = mk('clipPath', { id:id + 'c' }, defs), clip = mk('rect', { x:13, y:13, width:0, height:86 }, cp);
  mk('rect', { x:3, y:3, width:194, height:106, rx:7, fill:'url(#' + id + 'f)', stroke:'#6B7682', 'stroke-width':1.4 }, svg);
  mk('rect', { x:11, y:11, width:178, height:90, rx:2.5, fill:'url(#' + id + 's)', stroke:'#8E98A3', 'stroke-width':1 }, svg);
  mk('rect', { x:1, y:108, width:198, height:9, rx:3, fill:'#D5DADF', stroke:'#7E8892', 'stroke-width':1 }, svg);
  mk('line', { x1:6, y1:110.5, x2:194, y2:110.5, stroke:'#FFFFFF', 'stroke-width':1, opacity:0.8 }, svg);
  function pane(parent, x, handle, w, rx2) {
    w = w || 86; rx2 = rx2 || 0;
    mk('rect', { x:x, y:13, width:w, height:86, rx:1.8, fill:'#E9EDF1', stroke:'#4E5862', 'stroke-width':1.6 }, parent);
    var gl = mk('rect', { x:x + 5, y:18, width:w - 10, height:76, rx:1, fill:'#CFDAE5', stroke:'#8E98A3', 'stroke-width':0.8 }, parent);
    if (rx2) gl.setAttribute('width', w - 10 - rx2);
    mk('path', { d:'M' + (x + 14) + ' 30 L' + (x + 30) + ' 22 M' + (x + 14) + ' 40 L' + (x + 44) + ' 24', stroke:'#FFFFFF', 'stroke-width':2.2, 'stroke-linecap':'round', opacity:0.85 }, parent);
    mk('path', { d:'M' + (x + 52) + ' 88 L' + (x + 74) + ' 76', stroke:'#FFFFFF', 'stroke-width':1.6, 'stroke-linecap':'round', opacity:0.5 }, parent);
    if (handle) { var bs = x + 5 + (w - 10 - rx2), hx = bs + ((x + w) - bs - 5) / 2; mk('rect', { x:hx, y:47, width:5, height:20, rx:2.5, fill:'#4E5862' }, parent); mk('rect', { x:hx + 1.3, y:50, width:2.4, height:6, rx:1.2, fill:'#C3CAD1' }, parent); }
    return gl;
  }
  pane(svg, 94, false, 92);   // 고정창 왼쪽 틀을 움직이는 창 오른쪽 틀 밑으로 겹쳐 가운데가 한 줄로 보이게
  var wl = mk('g', { 'clip-path':'url(#' + id + 'c)' }, svg), wind = [];
  for (k = 0; k < 4; k++)
    wind.push({ p:mk('path', { d:'M0 0 q5 -4 10 0 t10 0 t10 0', fill:'none', stroke:'#6F86A0', 'stroke-width':2.4, 'stroke-linecap':'round', opacity:0 }, wl), t:k * 0.27, lane:30 + k * 18, sp:0.75 + (k % 2) * 0.35 });
  var sash = mk('g', {}, svg);
  mk('rect', { x:13, y:15, width:95, height:86, rx:2, fill:'rgba(30,40,51,.10)' }, sash);
  pane(sash, 13, true, 93, 6);   // 움직이는 창 오른쪽 틀을 넓혀 가운데 틀이 창 중앙(x≈100)에 오도록
  var sr = sash.firstChild.nextSibling;
  WINS.push({ zi:zi, clip:clip, sash:sash, sr:sr, pin:null, wind:wind, P:{ pns:'#4E5862' }, sw:1.6, sc0:'#4E5862' });
}
function buildWin(svg, zi, sty) {
  if (sty === 'j' || sty === 'jg') return buildWinJ(svg, zi, sty === 'jg');
  var nr = /-nr$/.test(sty || ''); if (nr) sty = sty.replace(/-nr$/, '');
  if (nr) return buildWinFine(svg, zi);
  var P = WPAL[sty] || WPAL.glass, cid = 'wc' + (++UID), k;
  svg.setAttribute('viewBox', nr ? '0 0 200 120' : '0 0 200 150');
  var defs = mk('defs', {}, svg), cp = mk('clipPath', { id:cid }, defs);
  var clip = mk('rect', { x:14, y:14, width:0, height:92 }, cp);
  mk('rect', { x:6, y:6, width:188, height:108, rx:5, fill:P.fr, stroke:P.frs, 'stroke-width':3 }, svg);
  mk('rect', { x:14, y:14, width:172, height:92, fill:P.sky }, svg);
  mk('rect', { x:100, y:14, width:86, height:92, fill:P.pn, stroke:P.pns, 'stroke-width':3 }, svg);
  mk('line', { x1:150, y1:28, x2:168, y2:46, stroke:P.gl, 'stroke-width':3, 'stroke-linecap':'round' }, svg);
  var wl = mk('g', { 'clip-path':'url(#' + cid + ')' }, svg), wind = [];
  for (k = 0; k < 4; k++)
    wind.push({ p:mk('path', { d:'M0 0 q5 -4 10 0 t10 0 t10 0', fill:'none', stroke:P.wd, 'stroke-width':3, 'stroke-linecap':'round', opacity:0 }, wl),
                t:k * 0.27, lane:32 + k * 18, sp:0.75 + (k % 2) * 0.35 });
  var sash = mk('g', {}, svg);
  var sr = mk('rect', { x:14, y:14, width:86, height:92, fill:P.pn, stroke:P.pns, 'stroke-width':3 }, sash);
  mk('line', { x1:28, y1:28, x2:46, y2:46, stroke:P.gl, 'stroke-width':3, 'stroke-linecap':'round' }, sash);
  mk('rect', { x:88, y:52, width:5, height:18, rx:2, fill:P.pns }, sash);
  var pin = null;
  if (!nr) {
  var d = 'M14 118';
  for (k = 0; k < 12; k++) { var rx = 14 + k * 7; d += ' L' + (rx + 2) + ' 118 L' + (rx + 3.5) + ' 123 L' + (rx + 5) + ' 118 L' + (rx + 7) + ' 118'; }
  mk('path', { d:d, stroke:P.rk, 'stroke-width':2, fill:'none' }, sash);
  pin = mk('g', {}, svg);
  mk('circle', { cx:100, cy:134, r:8, fill:P.rk }, pin);
  for (k = 0; k < 8; k++) mk('rect', { x:98.5, y:122.5, width:3, height:5, fill:P.rk, transform:'rotate(' + (k * 45) + ' 100 134)' }, pin);
  mk('circle', { cx:100, cy:134, r:2.4, fill:P.fr }, pin);
  }
  WINS.push({ zi:zi, clip:clip, sash:sash, sr:sr, pin:pin, wind:wind, P:P });
}
function animWin(o, dt) {
  var z = S.z[o.zi], off = z.win / 100 * 84;
  o.sash.setAttribute('transform', 'translate(' + off.toFixed(1) + ' 0)');
  if (o.pin) o.pin.setAttribute('transform', 'rotate(' + (off * 7).toFixed(1) + ' 100 134)');
  o.clip.setAttribute('width', off.toFixed(1));
  var sc = z.pin > 0 ? '#D21018' : (o.sc0 || o.P.pns);
  if (o._sc !== sc) { o.sr.setAttribute('stroke', sc); o.sr.setAttribute('stroke-width', z.pin > 0 ? (o.sw ? 3.5 : 5) : (o.sw || 3)); o._sc = sc; }
  var n = z.win <= 3 ? 0 : Math.min(4, Math.ceil(z.win / 25));
  if (o.jw) {
    n = z.win <= 3 ? 0 : Math.min(3, Math.ceil(z.win / 34));   // 창문 틈(움직이는 창 가장자리)에서 바람이 스윽 뻗어 나오고, 끝에서 꼬리가 위로 말림
    for (var j = 0; j < o.wind.length; j++) {
      var q = o.wind[j];
      if (!q.len) { try { q.len = q.p.getTotalLength(); } catch (er) { q.len = 80; } q.p.setAttribute('stroke-dasharray', q.len + ' ' + (q.len + 10)); }
      if (j >= n) { if (q._o !== 0) { q.p.setAttribute('opacity', 0); q._o = 0; } continue; }
      q.t += dt * q.sp * (0.8 + S.fan / 100 * 0.6); if (q.t > 1) q.t -= 1;
      var ph = q.t, draw = Math.min(1, ph / 0.6), ease = 1 - Math.pow(1 - draw, 3);
      var op = ph < 0.08 ? ph / 0.08 : ph > 0.8 ? Math.max(0, (1 - ph) / 0.2) : 1;
      q.p.setAttribute('stroke-dashoffset', (q.len * (1 - ease)).toFixed(1));
      q.p.setAttribute('transform', 'translate(' + (12 + off - 3 - ph * 6).toFixed(1) + ' ' + q.lane + ')');
      q.p.setAttribute('opacity', (op * 0.95).toFixed(2)); q._o = 1;
    }
    return;
  }
  for (var k = 0; k < o.wind.length; k++) {
    var w = o.wind[k];
    if (k >= n) { if (w._o !== 0) { w.p.setAttribute('opacity', 0); w._o = 0; } continue; }
    w.t += dt * w.sp * (0.7 + S.fan / 100 * 0.7); if (w.t > 1) w.t -= 1;
    var x = o.dir ? (12 - 48 + w.t * (off + 48)) : (14 + off - w.t * (off + 34));
    w.p.setAttribute('transform', 'translate(' + x.toFixed(1) + ' ' + w.lane + ')');
    w.p.setAttribute('opacity', (Math.sin(w.t * Math.PI) * 0.9).toFixed(2)); w._o = 1;
  }
}

/* ---------------- 그림 부품: 환풍기 ---------------- */
function buildFan(svg, sty) {
  var hmi = sty === 'hmi', dk = sty === 'dark', cx = 50, cy = 50, R = 38, r0 = 11;
  svg.setAttribute('viewBox', '0 0 100 100');
  mk('rect', { x:4, y:4, width:92, height:92, rx:9, fill:dk ? '#2B3138' : hmi ? '#D9DDE1' : '#E8EDF2', stroke:dk ? '#8E98A3' : hmi ? '#7E8892' : '#98A3AF', 'stroke-width':2.4 }, svg);
  mk('circle', { cx:cx, cy:cy, r:43, fill:dk ? '#1F2429' : hmi ? '#CDD2D7' : '#DCE2E8', stroke:dk ? '#8E98A3' : hmi ? '#7E8892' : '#98A3AF', 'stroke-width':2 }, svg);
  var g = mk('g', {}, svg);
  var blade = 'M ' + pt(cx, cy, r0, -16) + ' Q ' + pt(cx, cy, R * 0.72, -4) + ' ' + pt(cx, cy, R, 4) + ' A ' + R + ' ' + R + ' 0 0 1 ' + pt(cx, cy, R, 44) +
              ' Q ' + pt(cx, cy, R * 0.66, 40) + ' ' + pt(cx, cy, r0, 16) + ' A ' + r0 + ' ' + r0 + ' 0 0 0 ' + pt(cx, cy, r0, -16) + ' Z';
  for (var i = 0; i < 5; i++) mk('path', { d:blade, fill:hmi ? '#6E7882' : '#7B8894', stroke:hmi ? '#5A646E' : '#6A7682', 'stroke-width':1.2, transform:'rotate(' + (i * 72) + ' 50 50)' }, g);
  mk('circle', { cx:cx, cy:cy, r:12, fill:hmi ? '#AEB6BE' : '#B3BDC7', stroke:'#6A7682', 'stroke-width':1.2 }, svg);
  mk('circle', { cx:cx, cy:cy, r:4.5, fill:'#E7ECF0' }, svg);
  for (var y = 12; y <= 88; y += 6) mk('line', { x1:8, y1:y, x2:92, y2:y, stroke:dk ? '#3A424B' : hmi ? '#E1E4E7' : '#D3D9DF', 'stroke-width':1.2, opacity:0.75 }, svg);
  FANS.push({ g:g, a:0 });
}
function animFan(f, dt) {       // 한 프레임 회전이 날개 간격 절반(36°)을 넘으면 거꾸로 도는 것처럼 보이므로 초당 300° 이하
  f.a = (f.a - S.fan * 3.0 * dt) % 360;
  f.g.setAttribute('transform', 'rotate(' + f.a.toFixed(1) + ' 50 50)');
}

/* ---------------- 그림 부품: 최근 2분 위험지수 ---------------- */
function buildSpark(svg, zi, sty) {
  svg.setAttribute('viewBox', '0 0 244 160');
  var hmi = sty === 'hmi', nm = ['주의', '경고', '위험'];
  mk('rect', { x:0, y:0, width:244, height:150, fill:hmi ? '#E3E6E9' : 'rgba(255,255,255,.5)' }, svg);
  for (var i = 0; i < 3; i++) {
    var y = 150 - (25 * (i + 1)) * 1.5;
    mk('line', { x1:0, y1:y, x2:244, y2:y, stroke:hmi ? '#8F99A3' : LV_COL[i + 1], 'stroke-width':1.5, 'stroke-dasharray':'5 4' }, svg);
    var t = mk('text', { x:240, y:y - 4, 'text-anchor':'end', fill:hmi ? '#3E4852' : LV_COL[i + 1], 'font-size':12, 'font-weight':700 }, svg); t.textContent = nm[i];
  }
  var area = mk('path', { d:'M0 150', fill:'#5F6A75', opacity:0.16 }, svg);
  var line = mk('path', { d:'M0 150', fill:'none', stroke:'#2B3642', 'stroke-width':2.4, 'stroke-linejoin':'round' }, svg);
  var t1 = mk('text', { x:0, y:162, fill:'#5F6A75', 'font-size':12 }, svg); t1.textContent = '2분 전';
  var t2 = mk('text', { x:244, y:162, fill:'#5F6A75', 'font-size':12, 'text-anchor':'end' }, svg); t2.textContent = '지금';
  SPARKS.push({ zi:zi, area:area, line:line });
}
function drawSpark(s) {
  var h = S.z[s.zi].hist, n = h.length; if (n < 2) return;
  var d = '', x0 = 0;
  for (var k = 0; k < n; k++) {
    var x = 244 - (n - 1 - k) * 244 / 119, y = 150 - h[k] * 1.5;
    if (k === 0) x0 = x;
    d += (k ? ' L' : 'M') + x.toFixed(1) + ' ' + y.toFixed(1);
  }
  s.line.setAttribute('d', d);
  s.area.setAttribute('d', d + ' L244 150 L' + x0.toFixed(1) + ' 150 Z');
  var lv = S.z[s.zi].lv;
  s.area.setAttribute('fill', lv > 0 ? LV_COL[lv] : '#5F6A75'); s.area.setAttribute('opacity', lv > 0 ? 0.3 : 0.16);
}

/* ---------------- 5번 평면도 전용 ---------------- */
var PLAN = null;
function buildPWin(g, zi) {          // 위에서 본 미닫이: 벽 개구부 160, 고정창 오른쪽, 움직이는 창이 오른쪽으로 밀림
  mk('rect', { x:-2, y:-8, width:164, height:16, fill:'#F8FAFB' }, g);
  mk('path', { d:'M0 -7 V7 M160 -7 V7', stroke:'#27313B', 'stroke-width':4 }, g);
  mk('rect', { x:80, y:-5, width:80, height:4, fill:'#7FA6C8' }, g);
  var sash = mk('rect', { x:0, y:1, width:80, height:5, fill:'#3F6E99' }, g);
  var ar = [];
  for (var k = 0; k < 3; k++) ar.push({ p:mk('path', { d:'M-9 5 L0 -4 L9 5', fill:'none', stroke:'#2B3642', 'stroke-width':3.5, 'stroke-linecap':'round', 'stroke-linejoin':'round', opacity:0 }, g), t:k / 3 });
  PWINS.push({ zi:zi, sash:sash, ar:ar });
}
function initPlan(st) {
  var svg = st.querySelector('svg.pl'); if (!svg) { PLAN = null; return; }
  var pk = svg.querySelector('g.pk'), fc = svg.querySelector('g.flowc'), k, P = { pk:[], fc:[], rings:[], t:0, emit:0, ti:0 };
  for (k = 0; k < 6; k++) P.pk.push({ c:mk('circle', { r:6, cx:-20, cy:-20, fill:'#00C29B', stroke:'#FFF', 'stroke-width':2 }, pk), on:false });
  for (k = 0; k < 5; k++) P.fc.push({ p:mk('path', { d:'M-6 -9 L4 0 L-6 9', fill:'none', stroke:'#7B8894', 'stroke-width':3.5, 'stroke-linecap':'round', 'stroke-linejoin':'round', opacity:0 }, fc), t:k / 5 });
  var rs = svg.querySelectorAll('[data-ring]');
  for (k = 0; k < rs.length; k++) P.rings.push({ el:rs[k], zi:+rs[k].getAttribute('data-ring'), t:0 });
  PLAN = P;
}
var PK_X = { main:86, z0:222, z1:607, fan:800 };
function animPlan(dt) {
  var P = PLAN; if (!P) return; var k;
  P.emit -= dt;
  if (P.emit <= 0) {                                 // 메인 → 노드 명령, 노드 → 메인 응답 패킷을 번갈아
    var tg = ['z0', 'z1', 'fan'][P.ti % 3], back = Math.floor(P.ti / 3) % 2 === 1; P.ti++;
    for (k = 0; k < P.pk.length; k++) if (!P.pk[k].on) {
      var q = P.pk[k]; q.on = true; q.x0 = back ? PK_X[tg] : PK_X.main; q.x1 = back ? PK_X.main : PK_X[tg]; q.x = q.x0;
      var lv = tg === 'fan' ? (S.fanCmd > 50 ? allLv() : 0) : S.z[+tg.charAt(1)].lv;
      if (nodeSt(tg) === 'bad') { q.on = false; break; }
      q.c.setAttribute('fill', LV_COL[lv]); break;
    }
    P.emit = allLv() > 0 ? 0.22 : 0.45;
  }
  for (k = 0; k < P.pk.length; k++) {
    var p = P.pk[k]; if (!p.on) continue;
    var dir = p.x1 > p.x0 ? 1 : -1; p.x += dir * 620 * dt;
    if ((dir > 0 && p.x >= p.x1) || (dir < 0 && p.x <= p.x1)) { p.on = false; p.c.setAttribute('cx', -20); continue; }
    p.c.setAttribute('cx', p.x.toFixed(1)); p.c.setAttribute('cy', 50);
  }
  for (k = 0; k < PWINS.length; k++) {
    var w = PWINS[k], z = S.z[w.zi], off = z.win / 100 * 80;
    w.sash.setAttribute('x', off.toFixed(1));
    for (var j = 0; j < w.ar.length; j++) {
      var a = w.ar[j];
      if (z.win <= 3) { a.p.setAttribute('opacity', 0); continue; }
      a.t += dt * 0.7; if (a.t > 1) a.t -= 1;
      a.p.setAttribute('transform', 'translate(' + (off / 2).toFixed(1) + ' ' + (34 - a.t * 70).toFixed(1) + ')');
      a.p.setAttribute('opacity', (Math.sin(a.t * Math.PI) * 0.85).toFixed(2));
      a.p.setAttribute('stroke', z.lv > 0 ? LV_COL[z.lv] : '#2B3642');
    }
  }
  for (k = 0; k < P.fc.length; k++) {
    var c = P.fc[k];
    if (S.fan < 10) { c.p.setAttribute('opacity', 0); continue; }
    c.t += dt * 0.25 * S.fan / 100; if (c.t > 1) c.t -= 1;
    c.p.setAttribute('transform', 'translate(' + (110 + c.t * 660).toFixed(1) + ' 595)');
    c.p.setAttribute('opacity', (Math.sin(c.t * Math.PI) * 0.8).toFixed(2));
  }
  for (k = 0; k < P.rings.length; k++) {
    var r = P.rings[k], lz = S.z[r.zi].lv;
    if (lz === 0) { r.el.setAttribute('r', 0); continue; }
    r.t += dt * (0.6 + lz * 0.3); if (r.t > 1) r.t -= 1;
    r.el.setAttribute('r', (10 + r.t * 46).toFixed(1));
    r.el.setAttribute('stroke', LV_COL[lz]); r.el.setAttribute('opacity', (1 - r.t).toFixed(2));
  }
}

/* ---------------- 추세 화살표, 미니 추세, 큰 추세, 활성 경보 목록 ---------------- */
function causeShort(i) {
  var z = S.z[i], a = lvOf('co', z.co), b = lvOf('tvoc', z.tvoc);
  return (a >= b) ? 'CO ' + fmt(z.co) + ' ppm' : 'TVOC ' + fmt(z.tvoc) + ' ppb';
}
function trendTxt(h, now, eps) {
  if (!h || h.length < 11) return '유지';
  var d = now - h[h.length - 11];
  return d > eps ? '▲ 상승' : d < -eps ? '▼ 하강' : '유지';
}
/* ---------- E형: 실시간 미니 그래프 (선 / 막대) ---------- */
function spSeries(key) {
  var m = /^z(\d)\.(co|tvoc)$/.exec(key);
  if (m) { var z = S.z[+m[1]]; return { h:m[2] === 'co' ? z.hco : z.htv, k:m[2], v:z[m[2]] }; }
  return { h:S.hs ? S.hs[key] : null, k:(key === 'pm' || key === 'co2') ? key : null, v:S[key] };
}
function buildSp(svg, key, type) {
  var W = 240, H = 56;
  svg.setAttribute('viewBox', '0 0 ' + W + ' ' + H); svg.setAttribute('preserveAspectRatio', 'none');
  var s = spSeries(key), o = { key:key, type:type || 'line', W:W, H:H, k:s.k };
  if (s.k && o.type === 'line') for (var i = 1; i <= 3; i++) mk('line', { x1:0, y1:H - i * H / 4, x2:W, y2:H - i * H / 4, stroke:LV_COL[i], 'stroke-width':1, 'stroke-dasharray':'4 4', opacity:0.55, 'vector-effect':'non-scaling-stroke' }, svg);
  if (o.type === 'bar') { o.bars = []; for (var b = 0; b < 30; b++) o.bars.push(mk('rect', { x:b * 8 + 1, y:H, width:6, height:0, rx:1 }, svg)); }
  else {
    o.area = mk('path', { d:'M0 ' + H, opacity:0.16 }, svg);
    o.line = mk('path', { d:'M0 ' + H, fill:'none', 'stroke-width':2.2, 'stroke-linejoin':'round', 'vector-effect':'non-scaling-stroke' }, svg);
  }
  return o;
}
function drawSp(o) {
  var s = spSeries(o.key), h = s.h; if (!h || h.length < 2) return;
  var n = h.length, lv = s.k ? lvOf(s.k, s.v) : 0, col = lv > 0 ? LV_COL[lv] : (s.k ? '#4B5058' : (DES[CUR].gsp ? '#7A7A7A' : '#4F7EA6')), i, y;
  var norm;
  if (s.k) norm = function (v) { return gN(s.k, v) / 100; };
  else { var mn = 1e9, mx = -1e9; for (i = 0; i < n; i++) { if (h[i] < mn) mn = h[i]; if (h[i] > mx) mx = h[i]; } var pad = Math.max(0.6, (mx - mn) * 0.3); mn -= pad; mx += pad; norm = function (v) { return (v - mn) / (mx - mn); }; }
  if (o.type === 'bar') {
    for (var b = 0; b < 30; b++) {
      var v = h[Math.max(0, n - 30 + b)], hh = Math.max(2, norm(v) * (o.H - 4));
      o.bars[b].setAttribute('y', (o.H - hh).toFixed(1)); o.bars[b].setAttribute('height', hh.toFixed(1));
      var bl = s.k ? lvOf(s.k, v) : 0; o.bars[b].setAttribute('fill', bl > 0 ? LV_COL[bl] : '#7E8892');
    }
    return;
  }
  var d = '', x0 = 0;
  for (i = 0; i < n; i++) { var x = o.W - (n - 1 - i) * o.W / 119; y = o.H - 2 - norm(h[i]) * (o.H - 4); if (!i) x0 = x; d += (i ? ' L' : 'M') + x.toFixed(1) + ' ' + y.toFixed(1); }
  o.line.setAttribute('d', d); o.line.setAttribute('stroke', col);
  o.area.setAttribute('d', d + ' L' + o.W + ' ' + o.H + ' L' + x0.toFixed(1) + ' ' + o.H + ' Z'); o.area.setAttribute('fill', col);
}
function buildMini(svg, key) {
  svg.setAttribute('viewBox', '0 0 240 44'); svg.setAttribute('preserveAspectRatio', 'none');
  for (var i = 1; i <= 3; i++) mk('line', { x1:0, y1:44 - i * 11, x2:240, y2:44 - i * 11, stroke:LV_COL[i], 'stroke-width':1, 'stroke-dasharray':'4 4', opacity:0.7 }, svg);
  var area = mk('path', { d:'M0 44', fill:'#57606B', opacity:0.15 }, svg);
  var line = mk('path', { d:'M0 44', fill:'none', stroke:'#2E3842', 'stroke-width':2, 'stroke-linejoin':'round' }, svg);
  var m = /^z(\d)\.(co|tvoc)$/.exec(key);
  return { zi:+m[1], k:m[2], area:area, line:line };
}
function drawMini(o) {
  var z = S.z[o.zi], h = o.k === 'co' ? z.hco : z.htv; if (!h || h.length < 2) return;
  var n = h.length, d = '', x0 = 0;
  for (var k = 0; k < n; k++) { var x = 240 - (n - 1 - k) * 240 / 119, y = 44 - gN(o.k, h[k]) * 0.44; if (!k) x0 = x; d += (k ? ' L' : 'M') + x.toFixed(1) + ' ' + y.toFixed(1); }
  o.line.setAttribute('d', d); o.area.setAttribute('d', d + ' L240 44 L' + x0.toFixed(1) + ' 44 Z');
  var lv = lvOf(o.k, z[o.k]);
  o.line.setAttribute('stroke', lv > 0 ? LV_COL[lv] : '#2E3842');
  o.area.setAttribute('fill', lv > 0 ? LV_COL[lv] : '#57606B'); o.area.setAttribute('opacity', lv > 0 ? 0.25 : 0.15);
}
function buildTrend(svg) {
  svg.setAttribute('viewBox', '0 0 820 330');
  var Y = function (v) { return 300 - v * 2.8; };
  var band = ['#57606B', '#F4AC00', '#E55A00', '#D21018'], nm = ['정상', '주의', '경고', '위험'];
  for (var i = 0; i < 4; i++) {
    mk('rect', { x:60, y:Y((i + 1) * 25), width:750, height:70, fill:band[i], opacity:i ? 0.10 : 0.05 }, svg);
    var t = mk('text', { x:50, y:Y(i * 25 + 12.5) + 5, 'text-anchor':'end', fill:i ? band[i] : '#46525E', 'font-size':15, 'font-weight':800 }, svg); t.textContent = nm[i];
    if (i) mk('line', { x1:60, y1:Y(i * 25), x2:810, y2:Y(i * 25), stroke:band[i], 'stroke-width':1.5, 'stroke-dasharray':'6 5' }, svg);
  }
  mk('line', { x1:60, y1:300, x2:810, y2:300, stroke:'#7E8892', 'stroke-width':1.5 }, svg);
  var a = mk('text', { x:60, y:322, fill:'#46525E', 'font-size':14 }, svg); a.textContent = '2분 전';
  var b = mk('text', { x:810, y:322, fill:'#46525E', 'font-size':14, 'text-anchor':'end' }, svg); b.textContent = '지금';
  var l1 = mk('path', { d:'M60 300', fill:'none', stroke:'#1F2933', 'stroke-width':3.5, 'stroke-linejoin':'round' }, svg);
  var l2 = mk('path', { d:'M60 300', fill:'none', stroke:'#1F2933', 'stroke-width':3.5, 'stroke-dasharray':'10 6', 'stroke-linejoin':'round' }, svg);
  var d1 = mk('circle', { r:7, cx:-20, cy:-20, fill:'#1F2933', stroke:'#FFF', 'stroke-width':2 }, svg);
  var d2 = mk('circle', { r:7, cx:-20, cy:-20, fill:'#FFF', stroke:'#1F2933', 'stroke-width':3 }, svg);
  return { l:[l1, l2], d:[d1, d2], Y:Y };
}
function drawTrend(o) {
  for (var i = 0; i < 2; i++) {
    var h = S.z[i].hist, n = h.length; if (n < 2) continue;
    var d = '';
    for (var k = 0; k < n; k++) { var x = 810 - (n - 1 - k) * 750 / 119, y = o.Y(h[k]); d += (k ? ' L' : 'M') + x.toFixed(1) + ' ' + y.toFixed(1); }
    o.l[i].setAttribute('d', d); o.d[i].setAttribute('cx', 810); o.d[i].setAttribute('cy', o.Y(h[n - 1]).toFixed(1));
  }
}
function alarmHtml(nmax) {
  var L = [], i;
  for (i = 0; i < S.z.length; i++) {
    if (nodeSt('z' + i) === 'bad') L.push([3, ZONES[i].win + ' 노드 응답 없음']);
    if (S.z[i].lv > 0) L.push([S.z[i].lv, ZONES[i].nm + ', ' + causeShort(i)]);
  }
  if (lvOf('pm', S.pm) > 0) L.push([lvOf('pm', S.pm), '미세먼지 ' + fmt(S.pm) + ' µg/m³']);
  if (lvOf('co2', S.co2) > 0) L.push([lvOf('co2', S.co2), '이산화탄소 ' + fmt(S.co2) + ' ppm']);
  L.sort(function (a, b) { return b[0] - a[0]; });
  if (!L.length) return '<div class="al0">활성 경보 없음</div>';
  var h = '';
  for (i = 0; i < L.length && i < nmax; i++) h += '<div class="alr lv' + L[i][0] + '"><b>' + LV_NAME[L[i][0]] + '</b>' + L[i][1] + '</div>';
  if (L.length > nmax) h += '<div class="al0">외 ' + (L.length - nmax) + '건</div>';
  return h;
}

/* ---------------- 7번 경보 포커스 전용 ---------------- */
function tickFocus(st) {
  var tiles = st.querySelectorAll('[data-tile]'); if (tiles.length < 2) return;
  var f = -1, best = 0, i;
  for (i = 0; i < S.z.length; i++) {
    var sc = S.z[i].lv * 10 + (nodeSt('z' + i) === 'bad' ? 25 : 0) + zIdx(i) / 100;
    if ((S.z[i].lv > 0 || nodeSt('z' + i) === 'bad') && sc > best) { best = sc; f = i; }
  }
  var W = [681, 681]; if (f >= 0) { W[f] = 900; W[1 - f] = 462; }
  var x = 24;
  for (i = 0; i < tiles.length; i++) {
    var t = tiles[i];
    t.style.left = x + 'px'; t.style.width = W[i] + 'px'; x += W[i] + 12;
    setCls(t, /(^|\s)(f|m)(?=\s|$)/g, f < 0 ? '' : (i === f ? 'f' : 'm'));
  }
}

/* ---------------- 시안 목록 ---------------- */
var DES = {
  1:{ nm:'원안 다듬기' }, 2:{ nm:'구역 스트립' }, 3:{ nm:'고성능 HMI' }, 4:{ nm:'감지→판단→구동' },
  5:{ nm:'평면도', init:initPlan, anim:animPlan }, 6:{ nm:'세로 컬럼' }, 7:{ nm:'경보 포커스', tick:tickFocus },
  8:{ nm:'스트립 HMI', cls:'hp' }, 9:{ nm:'컬럼 HMI', cls:'hp' }, 10:{ nm:'HMI 개선판', cls:'hp' },
  15:{ nm:'최종 스트립', cls:'hm L8' },
  16:{ nm:'ISA-101 회색 HMI', cls:'hm L8 d15 hA', norm:'#57606B', ws:'hmi', fs:'hmi' },
  17:{ nm:'다크 제어실 HMI', cls:'hm L8 d15 hB', norm:'#7C8894', ws:'dark', fs:'dark', numc:['', '#FFB81C', '#FF7A29', '#FF4D55'] },
  18:{ nm:'페이스플레이트 HMI', cls:'hm L8 d15 hC', norm:'#4A5562', ws:'hmi', fs:'hmi' },
  52:{ nm:'최종', cls:'ee e2 f hh nx2 ii jj kk k2 nn fin', norm:'#4B5058', ws:'j', gsp:1, numc:['', '#FFB21E', '#FF7A2E', '#FF5257'] },
  51:{ nm:'N', cls:'ee e2 f hh nx2 ii jj kk k2 mm nn', norm:'#2B2B2D', ws:'j', gsp:1, numc:['', '#FFB21E', '#FF7A2E', '#FF5257'] },
  50:{ nm:'M', cls:'ee e2 f hh nx2 ii jj kk k2 mm', norm:'#2B2B2D', ws:'j', gsp:1, numc:['', '#FFB21E', '#FF7A2E', '#FF5257'] },
  49:{ nm:'L', cls:'ee e2 f hh nx2 ii jj kk k2', norm:'#2B2B2D', ws:'j', gsp:1, fs:'hmi', numc:['', '#FFB21E', '#FF7A2E', '#FF5257'] },
  48:{ nm:'K2', cls:'ee e2 f hh nx2 ii jj kk k2', norm:'#2B2B2D', ws:'j', fs:'hmi', numc:['', '#FFB21E', '#FF7A2E', '#FF5257'] },
  47:{ nm:'K', cls:'ee e2 f hh nx2 ii jj kk', norm:'#2B2B2D', ws:'j', fs:'hmi', numc:['', '#FFB21E', '#FF7A2E', '#FF5257'] },
  45:{ nm:'J2', cls:'ee e2 f hh nx2 ii jj lt jsq', norm:'#2B2B2D', ws:'j', fs:'hmi' },
  46:{ nm:'J3', cls:'ee e2 f hh nx2 ii jj lt jrd', norm:'#2B2B2D', ws:'j', fs:'hmi' },
  44:{ nm:'J', cls:'ee e2 f hh nx2 ii jj', norm:'#2B2B2D', ws:'j', fs:'hmi', numc:['', '#FFB21E', '#FF7A2E', '#FF5257'] },
  43:{ nm:'I', cls:'ee e2 f hh nx2 ii', norm:'#2B2B2D', ws:'hmi-nr', fs:'hmi', numc:['', '#FFB21E', '#FF7A2E', '#FF5257'] },
  41:{ nm:'H1', cls:'ee e2 f nx', norm:'#3A3D44', ws:'hmi-nr', fs:'hmi' },
  42:{ nm:'H2', cls:'ee e2 f hh nx2', norm:'#2B2B2D', ws:'hmi-nr', fs:'hmi', numc:['', '#FFB21E', '#FF7A2E', '#FF5257'] },
  40:{ nm:'H', cls:'ee e2 f hh', norm:'#2B2B2D', ws:'hmi-nr', fs:'hmi', numc:['', '#FFB21E', '#FF7A2E', '#FF5257'] },
  39:{ nm:'G', cls:'ee e2 f', norm:'#57606B', ws:'hmi-nr', fs:'hmi' },
  36:{ nm:'E1 스트립', cls:'ee e1', norm:'#57606B', ws:'hmi', fs:'hmi' },
  37:{ nm:'E2 세로 구역', cls:'ee e2', norm:'#57606B', ws:'hmi', fs:'hmi' },
  30:{ nm:'개선 1 정보 재배치', cls:'hm L8 d15 hA d20 nob', norm:'#57606B', ws:'hmi', fs:'hmi' },
  31:{ nm:'개선 2 추세 결합', cls:'hm L8 d15 hA d20 nob d19', norm:'#57606B', ws:'hmi', fs:'hmi' },
  32:{ nm:'개선 3 원거리 가독성', cls:'hm L8 d15 hA d20 nob fv', norm:'#57606B', ws:'hmi', fs:'hmi' },
  29:{ nm:'최종안 D', cls:'hm L8 d15 hA d20 nob', norm:'#57606B', ws:'hmi', fs:'hmi' },
  23:{ nm:'최종안 B', cls:'hm L8 d15 hA d20', norm:'#57606B', ws:'hmi', fs:'hmi' },
  24:{ nm:'최종안 C', cls:'hm L8 d15 hA d20 nob', norm:'#57606B', ws:'hmi', fs:'hmi' },
  25:{ nm:'1 통합 B', cls:'hm L8 d15 hA d20 rf', norm:'#57606B', ws:'hmi', fs:'hmi' },
  26:{ nm:'2 통합 C', cls:'hm L8 d15 hA d20 rf nob', norm:'#57606B', ws:'hmi', fs:'hmi' },
  27:{ nm:'3 참고 B', cls:'hm L8 d15 hA d20 rs', norm:'#3A3733', ws:'hmi', fs:'hmi', numc:['', '#E0921E', '#E0621F', '#C9302C'] },
  28:{ nm:'4 참고 C', cls:'hm L8 d15 hA d20 rs nob', norm:'#3A3733', ws:'hmi', fs:'hmi', numc:['', '#E0921E', '#E0621F', '#C9302C'] },
  19:{ nm:'추세 내장 스트립', cls:'hm L8 d15 hA', norm:'#57606B', ws:'hmi', fs:'hmi' },
  20:{ nm:'원인 강조 스트립', cls:'hm L8 d15 hA', norm:'#57606B', ws:'hmi', fs:'hmi' },
  21:{ nm:'개요 + 추세', cls:'hm L8 d15 hA fx', norm:'#57606B', ws:'hmi', fs:'hmi' },
  22:{ nm:'공정 미믹', cls:'hm L8 d15 hA fx', norm:'#57606B', ws:'hmi', fs:'hmi' },
  11:{ nm:'스트립 원래 스타일', cls:'gl L8' }, 12:{ nm:'컬럼 원래 스타일', cls:'gl L9' },
  13:{ nm:'스트립 현대 HMI', cls:'hm L8' }, 14:{ nm:'컬럼 현대 HMI', cls:'hm L9' }
};
var CUR = 1, B = null;
var HDR = '<div class="hdr">' +
  '<div class="clk" data-v="clock">--:--</div>' +
  '<div class="hdt"><svg class="wifi" data-lv="link" viewBox="-30 92 572 396"><g fill="none" stroke-width="62" stroke-linecap="round"><path d="M19 241 A310 310 0 0 1 493 241"/><path d="M95 305 A210 210 0 0 1 417 305"/><path d="M172 369 A110 110 0 0 1 340 369"/></g><circle cx="256" cy="440" r="34"/></svg>' +
  '<div class="dt" data-v="date"></div><div class="brand">Smart AirGuard <b data-v="sys" data-lv="link"></b></div></div>' +
  '<div class="pill" data-lv="all"><span>종합 위험</span><b data-v="lvname"></b><em data-v="lvnum"></em></div>' +
  '<div class="mn"><div class="mnt">메인 제어기</div><span class="chip" data-node="main"></span><span class="nid">0x100</span></div>' +
  '<div class="conn"><div><i class="cd" data-lv="link"></i><span data-v="conn"></span><span class="demo">데모</span></div><div class="upd" data-v="upd"></div></div>' +
  '</div>';
function expand(t) {
  t = t.replace('{{HDR}}', HDR);
  return t.replace(/\[\[Z\]\]([\s\S]*?)\[\[\/Z\]\]/g, function (m, body) {
    var out = '';
    for (var i = 0; i < ZONES.length; i++) {
      out += body.replace(/\{at:(-?\d+):(-?\d+)\}/g, function (mm, a, b) { return String(+a + i * +b); })
                 .replace(/\{i\}/g, String(i)).replace(/\{n\}/g, String(i + 1))
                 .replace(/\{nm\}/g, ZONES[i].nm).replace(/\{win\}/g, ZONES[i].win).replace(/\{id\}/g, ZONES[i].id);
    }
    return out;
  });
}
function grab(st, attr) {      // 한 요소에 data-v 와 data-lv 가 같이 있어도 키가 섞이지 않게 속성마다 따로 보관
  var r = [], l = st.querySelectorAll('[' + attr + ']'), p = '_' + attr.replace('data-', '');
  for (var i = 0; i < l.length; i++) { l[i][p] = l[i].getAttribute(attr); r.push(l[i]); } return r; }
function mount(n) {
  if (!$('t' + n)) n = window.DEF_D || 1;
  CUR = n;
  var st = $('stage');
  st.className = 'd' + n + (DES[n].cls ? ' ' + DES[n].cls : '') + (NORM === 'd' ? ' nB' : '');
  LV_COL[0] = DES[n].norm || NORM_COL[NORM];
  st.innerHTML = expand($('t' + n).text || $('t' + n).innerHTML);
  GAUGES = []; WINS = []; FANS = []; SPARKS = []; PWINS = []; PLAN = null;
  B = { v:grab(st, 'data-v'), c:grab(st, 'data-c'), lv:grab(st, 'data-lv'), bar:grab(st, 'data-bar'), node:grab(st, 'data-node'), ptr:grab(st, 'data-ptr'), ev:grab(st, 'data-ev'), al:grab(st, 'data-al'), ms:[], tr:[] };
  var q = grab(st, 'data-ms'); for (var qi = 0; qi < q.length; qi++) B.ms.push(buildMini(q[qi], q[qi]._ms));
  B.lost = grab(st, 'data-lost');
  B.rg = []; var rq = grab(st, 'data-rg'); for (var ri = 0; ri < rq.length; ri++) B.rg.push(buildRing(rq[ri], rq[ri]._rg));
  B.sp = []; B.wb = grab(st, 'data-wb'); q = grab(st, 'data-sp'); for (qi = 0; qi < q.length; qi++) B.sp.push(buildSp(q[qi], q[qi]._sp, q[qi].getAttribute('data-spt')));
    q = grab(st, 'data-tr'); for (qi = 0; qi < q.length; qi++) B.tr.push(buildTrend(q[qi]));
  var i, l;
  l = grab(st, 'data-ico'); for (i = 0; i < l.length; i++) l[i].innerHTML = ICON[l[i]._ico] || '';
  l = grab(st, 'data-ix'); var colIco = /(^|\s)gl(\s|$)/.test(st.className);
  for (i = 0; i < l.length; i++) l[i].innerHTML = (colIco ? ICON : ICONM)[l[i]._ix] || '';
  l = grab(st, 'data-icm'); for (i = 0; i < l.length; i++) l[i].innerHTML = ICONM[l[i]._icm] || '';
  l = grab(st, 'data-gauge'); for (i = 0; i < l.length; i++) buildGauge(l[i], l[i]._gauge, l[i].getAttribute('data-gs'));
  l = grab(st, 'data-win'); for (i = 0; i < l.length; i++) buildWin(l[i], +l[i]._win, l[i].getAttribute('data-ws') || DES[n].ws);
  l = grab(st, 'data-fan'); for (i = 0; i < l.length; i++) buildFan(l[i], l[i].getAttribute('data-fs') || DES[n].fs);
  l = grab(st, 'data-spark'); for (i = 0; i < l.length; i++) buildSpark(l[i], +l[i]._spark, l[i].getAttribute('data-ss'));
  l = grab(st, 'data-pwin'); for (i = 0; i < l.length; i++) buildPWin(l[i], +l[i]._pwin);
  if (DES[n].init) DES[n].init(st);
  B.evv = -1;
  var bt = document.querySelectorAll('#tb button[data-d]');
  for (i = 0; i < bt.length; i++) { var on = +bt[i].getAttribute('data-d') === n; bt[i].className = on ? 'on' : ''; bt[i].setAttribute('aria-pressed', on ? 'true' : 'false'); }
  try { localStorage.setItem('ag_final2', String(n)); } catch (e) {}
  for (i = 0; i < SPARKS.length; i++) drawSpark(SPARKS[i]);
  for (i = 0; i < B.ms.length; i++) drawMini(B.ms[i]); for (i = 0; i < B.tr.length; i++) drawTrend(B.tr[i]); for (i = 0; i < B.sp.length; i++) drawSp(B.sp[i]);
  render();
}

/* ---------------- 값 → 화면 ---------------- */
function render() {
  if (!B) return;
  var i, e, t;
  for (i = 0; i < B.v.length; i++) { e = B.v[i]; t = V(e._v); if (e._t !== t) { e.textContent = t; e._t = t; } }
  for (i = 0; i < B.c.length; i++) {
    e = B.c[i]; var lv = LK(e._c), col = lv > 0 ? (DES[CUR].numc || NUM_COL)[lv] : '';
    if (e._col !== col) { if (e.namespaceURI === SVGNS) e.style.fill = col; else { e.style.color = col; e.style.textShadow = col ? '0 1px ' + (lv === 1 ? 3 : 2) + 'px rgba(0,0,0,.46), 0 0 1px rgba(0,0,0,.26)' : ''; } e._col = col; }
  }
  for (i = 0; i < B.lv.length; i++) setCls(B.lv[i], RE_LV, 'lv' + LK(B.lv[i]._lv));
  if (B.rg) for (i = 0; i < B.rg.length; i++) drawRing(B.rg[i]);
  if (B.lost) for (i = 0; i < B.lost.length; i++) setCls(B.lost[i], /(^|\s)lost(?=\s|$)/g, nodeSt(B.lost[i]._lost) === 'bad' ? 'lost' : '');
  for (i = 0; i < B.wb.length; i++) {
    e = B.wb[i]; var wz = S.z[+B.wb[i]._wb], wf = e._f || (e._f = e.getElementsByTagName('b')[0]), wk = e._k2 || (e._k2 = e.getElementsByTagName('i')[0]);
    var ww = clamp(wz.win, 0, 100).toFixed(1) + '%', wc = LV_COL[wz.lv];
    if (wf._w !== ww) { wf.style.width = ww; if (wk) wk.style.left = ww; wf._w = ww; }
    if (wf._c !== wc) { wf.style.backgroundColor = wc; if (wk) wk.style.borderColor = wc; wf._c = wc; }
  }
  for (i = 0; i < B.bar.length; i++) {
    e = B.bar[i]; var b = e._b || (e._b = e.getElementsByTagName('b')[0]); if (!b) continue;
    var pv = PK(e._bar), w = clamp(pv, 0, 100).toFixed(1) + '%', bc = LV_COL[Math.min(3, Math.floor(pv / 25))];
    if (b._w !== w) { b.style.width = w; b._w = w; } if (b._c !== bc) { b.style.backgroundColor = bc; b._c = bc; }
  }
  for (i = 0; i < B.ptr.length; i++) { e = B.ptr[i]; t = clamp(PK(e._ptr), 0, 100).toFixed(1) + '%'; if (e._t !== t) { e.style.left = t; e._t = t; } }
  for (i = 0; i < B.node.length; i++) {
    e = B.node[i]; var s = nodeSt(e._node); setCls(e, RE_ST, s);
    if (e.namespaceURI !== SVGNS && e._s !== s) { e.innerHTML = '<i></i>' + (s === 'ok' ? '연결됨' : s === 'bad' ? '응답 없음' : '미연결'); e._s = s; }
  }
  if (B.evv !== evVer) {
    B.evv = evVer;
    for (i = 0; i < B.ev.length; i++) {
      var h = '', nmax = +B.ev[i]._ev;
      for (var j = 0; j < EV.length && j < nmax; j++)
        h += '<div class="ev"><i style="background:' + LV_COL[EV[j].lv] + '"></i><span class="t">' + EV[j].t + '</span>' + EV[j].m + '</div>';
      B.ev[i].innerHTML = h;
    }
  }
  for (i = 0; i < B.al.length; i++) { var ah = alarmHtml(+B.al[i]._al); if (B.al[i]._h !== ah) { B.al[i].innerHTML = ah; B.al[i]._h = ah; } }
  if (DES[CUR].tick) DES[CUR].tick($('stage'));
  var allRed = S.z.length > 0;                         // 모든 구역이 위험이면 뒷배경 붉은 점멸
  for (i = 0; i < S.z.length; i++) if (S.z[i].lv < 3) allRed = false;
  var stg = $('stage'), want = allRed ? 'blk' : '';
  if (stg._blk !== want) { setCls(stg, /(^|\s)blk(?=\s|$)/g, want); stg._blk = want; }
}

/* ---------------- 화면 맞춤 ---------------- */
function fitStage() {
  var fit = $('fit'), tb = $('tb');
  if (!KIOSK && tb) fit.style.top = tb.offsetHeight + 'px';
  var w = fit.clientWidth, h = fit.clientHeight, pad = KIOSK ? 0 : 16;
  var s = Math.min((w - pad * 2) / 1422, (h - pad * 2) / 800);
  var st = $('stage'), tf = 'scale(' + s + ')';
  st.style.webkitTransform = tf; st.style.transform = tf;
  st.style.left = Math.round((w - 1422 * s) / 2) + 'px'; st.style.top = Math.round((h - 800 * s) / 2) + 'px';
}

/* ---------------- 시작 ---------------- */
(function () {
  if (KIOSK) document.body.className += ' kiosk';
  if (DEMO) document.body.className += ' demo';
  var m = /[?&]d=(\d+)/.exec(Q), n = m ? +m[1] : 0;
  if (!n) { try { n = +localStorage.getItem('ag_final2') || (window.DEF_D || 11); } catch (e) { n = 1; } }
  var bt = document.querySelectorAll('#tb button[data-d]');
  for (var i = 0; i < bt.length; i++) bt[i].onclick = function () { mount(+this.getAttribute('data-d')); };
  
  var nb = document.querySelectorAll('#tb button[data-nv]');
  for (i = 0; i < nb.length; i++) nb[i].onclick = function () { setNorm(this.getAttribute('data-nv')); };
  var sc = $('scn');
  if (sc) sc.onchange = function () { sim.mode = sc.value; sim.ph = 'idle'; sim.t = 0; sim.calm = 0; logEvent('미리보기 상황: ' + sc.options[sc.selectedIndex].text, 0); };
  logEvent('시스템 시작, 전 노드 정상', 0);
  S.hs = { pm:[], temp:[], hum:[], co2:[] }; for (var hq = 0; hq < 120; hq++) { S.hs.pm.push(S.pm); S.hs.temp.push(S.temp); S.hs.hum.push(S.hum); S.hs.co2.push(S.co2); }
  for (var hz = 0; hz < S.z.length; hz++) { var Z0 = S.z[hz]; Z0.hco = []; Z0.htv = []; for (var hk = 0; hk < 120; hk++) { Z0.hist.push(zIdx(hz)); Z0.hco.push(Z0.co); Z0.htv.push(Z0.tvoc); } }
  mount(n); setNorm(NORM);
  fitStage(); setTimeout(fitStage, 300); setTimeout(fitStage, 1500);
  window.onresize = fitStage;
  window.addEventListener('orientationchange', function () { setTimeout(fitStage, 300); });
  if (KIOSK) document.addEventListener('click', function () {      // 태블릿: 한 번 터치하면 전체 화면
    var d = document.documentElement, on = document.fullscreenElement || document.webkitFullscreenElement, req = d.requestFullscreen || d.webkitRequestFullscreen;
    if (!on && req) { try { req.call(d); } catch (e) {} } setTimeout(fitStage, 400);
  });
  var TICK = 0.2, acc = 0, hacc = 0;
  setInterval(function () {
    if (DEMO) simStep(TICK); else S.online = (new Date().getTime() - lastOk) < 3000;
    checkEvents();
    acc += TICK; hacc += TICK;
    if (acc >= 1) { acc = 0; if (!DEMO) poll(); }
    if (hacc >= 1) {
      hacc = 0;
      for (var i = 0; i < S.z.length; i++) {
        var zz = S.z[i]; if (!zz.hco) { zz.hco = []; zz.htv = []; }
        zz.hist.push(zIdx(i)); zz.hco.push(zz.co); zz.htv.push(zz.tvoc);
        if (zz.hist.length > 120) { zz.hist.shift(); } if (zz.hco.length > 120) { zz.hco.shift(); zz.htv.shift(); }
      }
      if (S.hs) { var hsk = ['pm', 'temp', 'hum', 'co2']; for (var hj = 0; hj < 4; hj++) { S.hs[hsk[hj]].push(S[hsk[hj]]); if (S.hs[hsk[hj]].length > 120) S.hs[hsk[hj]].shift(); } }
      if (B) { for (i = 0; i < B.sp.length; i++) drawSp(B.sp[i]); }
      if (B) { for (i = 0; i < B.ms.length; i++) drawMini(B.ms[i]); for (i = 0; i < B.tr.length; i++) drawTrend(B.tr[i]); }
      for (i = 0; i < SPARKS.length; i++) drawSpark(SPARKS[i]);
    }
    render();
  }, TICK * 1000);
  var FR = 0.04;
  setInterval(function () {
    var i;
    for (i = 0; i < WINS.length; i++) animWin(WINS[i], FR);
    for (i = 0; i < FANS.length; i++) animFan(FANS[i], FR);
    for (i = 0; i < GAUGES.length; i++) animGauge(GAUGES[i]);
    if (DES[CUR].anim) DES[CUR].anim(FR);
  }, FR * 1000);
})();
</script>
</body>
</html>
)AGHTML";
