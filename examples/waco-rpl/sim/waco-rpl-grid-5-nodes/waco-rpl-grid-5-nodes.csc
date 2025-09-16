<?xml version="1.0" encoding="UTF-8"?>
<simconf>
  <project EXPORT="discard">[APPS_DIR]/mrm</project>
  <project EXPORT="discard">[APPS_DIR]/mspsim</project>
  <project EXPORT="discard">[APPS_DIR]/avrora</project>
  <project EXPORT="discard">[APPS_DIR]/serial_socket</project>
  <project EXPORT="discard">[APPS_DIR]/collect-view</project>
  <project EXPORT="discard">[APPS_DIR]/powertracker</project>
  <project EXPORT="discard">[APPS_DIR]/radiologger-headless</project>
  <simulation>
    <title>waco-rpl-grid-5-nodes</title>
    <randomseed>123457</randomseed>
    <motedelay_us>1000000</motedelay_us>
    <radiomedium>
      org.contikios.cooja.radiomediums.UDGM
      <transmitting_range>50.0</transmitting_range>
      <interference_range>50.0</interference_range>
      <success_ratio_tx>1.0</success_ratio_tx>
      <success_ratio_rx>1.0</success_ratio_rx>
    </radiomedium>
    <events>
      <logoutput>40000</logoutput>
    </events>
    <motetype>
      org.contikios.cooja.mspmote.SkyMoteType
      <identifier>sky1</identifier>
      <description>Sky Mote Type #sky1</description>
      <source EXPORT="discard">[CONTIKI_DIR]/examples/waco-rpl/example-waco-rpl.c</source>
      <commands EXPORT="discard">make example-waco-rpl.sky TARGET=sky</commands>
      <firmware EXPORT="copy">[CONTIKI_DIR]/examples/waco-rpl/example-waco-rpl.sky</firmware>
      <moteinterface>org.contikios.cooja.interfaces.Position</moteinterface>
      <moteinterface>org.contikios.cooja.interfaces.RimeAddress</moteinterface>
      <moteinterface>org.contikios.cooja.interfaces.IPAddress</moteinterface>
      <moteinterface>org.contikios.cooja.interfaces.Mote2MoteRelations</moteinterface>
      <moteinterface>org.contikios.cooja.interfaces.MoteAttributes</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.MspClock</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.MspMoteID</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.SkyButton</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.SkyFlash</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.SkyCoffeeFilesystem</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.Msp802154Radio</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.WakeupRadio</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.MspSerial</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.SkyLED</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.MspDebugOutput</moteinterface>
      <moteinterface>org.contikios.cooja.mspmote.interfaces.SkyTemperature</moteinterface>
    </motetype>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>0.0</x>
        <y>0.0</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspClock
        <deviation>1.0</deviation>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspMoteID
        <id>1</id>
      </interface_config>
      <motetype_identifier>sky1</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>0.0</x>
        <y>30.0</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspClock
        <deviation>1.0</deviation>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspMoteID
        <id>2</id>
      </interface_config>
      <motetype_identifier>sky1</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>30.0</x>
        <y>0.0</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspClock
        <deviation>1.0</deviation>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspMoteID
        <id>3</id>
      </interface_config>
      <motetype_identifier>sky1</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>30.0</x>
        <y>30.0</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspClock
        <deviation>1.0</deviation>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspMoteID
        <id>4</id>
      </interface_config>
      <motetype_identifier>sky1</motetype_identifier>
    </mote>
    <mote>
      <breakpoints />
      <interface_config>
        org.contikios.cooja.interfaces.Position
        <x>60.0</x>
        <y>0.0</y>
        <z>0.0</z>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspClock
        <deviation>1.0</deviation>
      </interface_config>
      <interface_config>
        org.contikios.cooja.mspmote.interfaces.MspMoteID
        <id>5</id>
      </interface_config>
      <motetype_identifier>sky1</motetype_identifier>
    </mote>
  </simulation>
  <plugin>
    org.contikios.cooja.plugins.SimControl
    <width>280</width>
    <z>1</z>
    <height>160</height>
    <location_x>400</location_x>
    <location_y>0</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.ScriptRunner
    <plugin_config>
      <script>var LOG_DIR              = "/home/chuongvo/waco/examples/waco-rpl/sim/waco-rpl-grid-5-nodes";
var LOG_BASENAME         = "waco-rpl-grid-5-nodes";
var APPEND_TO_EXISTING   = false;
var ADD_TIMESTAMP_SUFFIX = true;
var SIM_SETTLING_TIME    = 240000;
TIMEOUT(1800000);

try { load("nashorn:mozilla_compat.js"); } catch(err) {}

importPackage(java.io);
importPackage(java.util);
var dir = new File(LOG_DIR); if (!dir.exists()) { dir.mkdirs(); }
var tsSuffix = "";
if (ADD_TIMESTAMP_SUFFIX) { var sdf = new java.text.SimpleDateFormat("yyyyMMdd-HHmmss"); tsSuffix = "-" + sdf.format(new java.util.Date()); }
var outFile = new File(dir, LOG_BASENAME + tsSuffix + ".txt");
var dcFile  = new File(dir, LOG_BASENAME + tsSuffix + "_dc.txt");
var allm   = sim.getMotes(); var nmotes = allm.length;
var ptplugin = sim.getCooja().getStartedPlugin("PowerTracker"); if (ptplugin != null) { ptplugin.reset(); }
var outputs   = new FileWriter(outFile, APPEND_TO_EXISTING);
var dcoutputs = new FileWriter(dcFile,  APPEND_TO_EXISTING);
GENERATE_MSG(SIM_SETTLING_TIME, "Simulation Settling Time");
outputs.write("=== START " + (new java.util.Date()) + " | motes=" + nmotes + " ===\n");
while (true) {
  if (typeof msg !== "undefined" &amp;&amp; msg.equals("Simulation Settling Time")) { if (ptplugin != null) { ptplugin.reset(); } }
  else if (typeof msg !== "undefined" &amp;&amp; typeof id !== "undefined") { outputs.write(time + "\tID:" + id + "\t" + msg + "\n"); }
  try { YIELD(); } catch (e) { if (ptplugin != null) { var stats = ptplugin.radioStatistics(); dcoutputs.write(stats + "\n"); } outputs.close(); dcoutputs.close(); throw('test script killed'); }
}</script>
      <active>true</active>
    </plugin_config>
    <width>600</width>
    <z>4</z>
    <height>700</height>
    <location_x>30</location_x>
    <location_y>30</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.Visualizer
    <plugin_config>
      <moterelations>true</moterelations>
      <skin>org.contikios.cooja.plugins.skins.IDVisualizerSkin</skin>
      <skin>org.contikios.cooja.plugins.skins.GridVisualizerSkin</skin>
      <skin>org.contikios.cooja.plugins.skins.TrafficVisualizerSkin</skin>
      <skin>org.contikios.cooja.plugins.skins.UDGMVisualizerSkin</skin>
      <viewport>5.878787878787879 0.0 0.0 5.878787878787879 17.636363636363637 84.81818181818183</viewport>
    </plugin_config>
    <width>400</width>
    <z>3</z>
    <height>400</height>
    <location_x>1242</location_x>
    <location_y>282</location_y>
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.LogListener
    <plugin_config>
      <filter>APP-DL</filter>
      <formatted_time />
      <coloring />
    </plugin_config>
    <width>3240</width>
    <z>2</z>
    <height>240</height>
    <location_x>400</location_x>
    <location_y>160</location_y>
  </plugin>
  <plugin>
    PowerTracker
    <width>400</width>
    <z>0</z>
    <height>400</height>
    <location_x>430</location_x>
    <location_y>190</location_y>
  </plugin>
</simconf>

