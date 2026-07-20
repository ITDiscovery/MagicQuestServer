#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h> 
#include <LittleFS.h> 
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>


#define DECODE_MAGIQUEST
#include <IRremote.hpp>

#define IR_RECEIVE_PIN 9 

WebServer server(80);
Preferences preferences;
Preferences wandPrefs; 
Preferences blePrefs;

String currentSSID;
String currentPassword;
String lastPayload = ""; 

// --- Pairing Mode Variables ---
bool isPairingMode = false;
String pendingCasterName = "";
String pendingStoryName = ""; // <-- NEW: Tracks the story during pairing
unsigned long pairingStartTime = 0;
const unsigned long PAIRING_TIMEOUT = 30000;

// --- Galaxy's Edge BLE Settings ---
uint8_t activeBeaconPreset = 1; // Default to Market
BLEAdvertising *pAdvertising;

// --- Sniffer Dashboard Variables ---
String lastSniffedHex = "Waiting for signal...";
String lastSniffedProtocol = "UNKNOWN";

// --- Main Webpage (Your MagiQuest UI) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>MagiQuest Display</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { background-color: #000; color: #fff; margin: 0; overflow: hidden; display: flex; height: 100vh; font-family: sans-serif; text-align: center; }
    #ui { width: 100vw; height: 100vh; display: flex; flex-direction: column; justify-content: center; align-items: center; position: absolute; top: 0; left: 0; z-index: 10; background: radial-gradient(circle, #2a0845 0%, #6441A5 100%); transition: opacity 0.5s; }
    h1 { font-size: 2.5rem; margin-bottom: 10px; }
    #actionBtn { font-size: 1.2rem; cursor: pointer; padding: 15px 30px; background: rgba(255,255,255,0.1); border: 2px solid #fff; border-radius: 10px; margin-top: 20px; transition: 0.3s; }
    #actionBtn:active { background: rgba(255,255,255,0.3); }
    #videoContainer { width: 100vw; height: 100vh; position: absolute; top: 0; left: 0; z-index: 1; background: #000; }
    iframe { width: 100%; height: 100%; border: none; }
    .admin-link { position: absolute; bottom: 10px; right: 10px; color: rgba(255,255,255,0.2); text-decoration: none; font-size: 0.8rem; }
  </style>
</head>
<body>
  
  <div id="ui">
    <h1>Welcome, Magi</h1>
    <div id="actionBtn" onclick="startMagic()">Tap Here to Activate the Crystal</div>
    <a href="/admin" class="admin-link">Dashboard</a>
  </div>

  <div id="videoContainer">
    <iframe id="ytPlayer" src="" allow="autoplay; fullscreen"></iframe>
  </div>

  <script>
    var pollingInterval;
    
    function startMagic() {
      var btn = document.getElementById('actionBtn');
      btn.innerText = "Crystal Active! Waiting for a spell...";
      btn.style.borderColor = "#4caf50"; btn.style.color = "#4caf50";
      pollingInterval = setInterval(checkWand, 500);
    }
    
    function checkWand() {
      fetch('/wand?t=' + new Date().getTime())
        .then(response => response.text())
        .then(data => {
          if (data !== "") { 
            var parts = data.split(',');
            var wandHex = parts[0];
            var videoId = parts[1];
            var casterName = parts[2]; 
            
            if (videoId === "UNKNOWN") videoId = "dQw4w9WgXcQ"; 

            // Fade the UI overlay back in for consecutive casts
            var ui = document.getElementById('ui');
            ui.style.display = 'flex';
            setTimeout(() => { ui.style.opacity = '1'; }, 50);

            var btn = document.getElementById('actionBtn');
            btn.innerText = casterName + " cast a spell!";
            btn.style.borderColor = "#ffeb3b"; btn.style.color = "#ffeb3b";

            // Wait 1.5 seconds, fade UI out, and update the iframe source!
            setTimeout(() => {
              ui.style.opacity = '0';
              setTimeout(() => { ui.style.display = 'none'; }, 500);
              
              // RECYCLING THE IFRAME: Update src with Unmuted audio and CC disabled!
              var embedUrl = "https://www.youtube.com/embed/" + videoId + "?autoplay=1&mute=0&controls=0&playsinline=1&cc_load_policy=0";
              document.getElementById('ytPlayer').src = embedUrl;
              
            }, 1500);
          }
        })
        .catch(error => console.log("Polling error", error));
    }
  </script>
</body>
</html>
)rawliteral";

// --- Shared CSS for the Backend Dashboard ---
const char dashboard_css[] PROGMEM = R"rawliteral(
  <style>
    body { background-color: #222; color: #fff; font-family: sans-serif; display: flex; height: 100vh; margin: 0; }
    .sidebar { width: 250px; background: #111; display: flex; flex-direction: column; padding-top: 20px; border-right: 2px solid #333; }
    .sidebar h2 { text-align: center; color: #9c27b0; margin-bottom: 30px; font-size: 1.5rem; letter-spacing: 2px; }
    .sidebar a { padding: 15px 25px; color: #aaa; text-decoration: none; font-size: 1.1rem; transition: 0.2s; border-left: 4px solid transparent; }
    .sidebar a:hover { background: #222; color: #fff; border-left: 4px solid #9c27b0; }
    .sidebar a.active { background: #2a0845; color: #fff; border-left: 4px solid #4caf50; font-weight: bold; }
    
    .content { flex: 1; padding: 40px; overflow-y: auto; display: flex; flex-direction: column; align-items: center; }
    .box { background: #333; padding: 25px; border-radius: 10px; text-align: center; width: 100%; max-width: 600px; margin-bottom: 30px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
    .box h2 { margin-top: 0; border-bottom: 1px solid #555; padding-bottom: 10px; }
    
    input, textarea { margin: 10px 0; padding: 12px; border-radius: 5px; border: 1px solid #555; background: #444; color: white; font-size: 1rem; }
    input::placeholder { color: #aaa; }
    button { padding: 12px 20px; color: white; border: none; border-radius: 5px; cursor: pointer; font-size: 1rem; transition: 0.2s; margin-top: 10px; }
    button:hover { opacity: 0.9; }
    .btn-green { background: #4caf50; }
    .btn-purple { background: #9c27b0; }
    .btn-red { background: #f44336; }
    .btn-small { padding: 6px 12px; font-size: 0.9rem; margin: 0 5px; }

    /* New CSS for the Story Editor Rows */
    .story-row { display: flex; gap: 10px; align-items: center; margin-bottom: 10px; background: #222; padding: 10px; border-radius: 5px; }
    .story-row input { margin: 0; }
    .page-badge { background: #9c27b0; padding: 5px 12px; border-radius: 3px; font-weight: bold; font-size: 1.1rem; }
  </style>
)rawliteral";

// --- Secret Admin Webpage ---
const char admin_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Admin Panel - MagiQuest</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  %DASHBOARD_CSS%
</head>
<body onload="fetchCasters()">
  <div class="sidebar">
    <h2>DASHBOARD</h2>
    <a href="/">Main (Story)</a>
    <a href="/author">Author Panel</a>
    <a href="/admin" class="active">Admin Panel</a>
  </div>
  
  <div class="content">
    
    <div class="box" style="max-width: 800px;">
      <h2>Manage Casters</h2>
      <p style="font-size: 0.9rem; color:#aaa;">Manage your registered magi, their stories, and current pages.</p>
      
      <div style="display: flex; gap: 10px; justify-content: center; margin-bottom: 20px;">
        <button onclick="fetchCasters()" class="btn-green">List Casters</button>
        <button onclick="showNewCasterForm()" class="btn-purple">New Caster</button>
      </div>

      <div id="casterWorkspace" style="text-align: left; background: #444; padding: 20px; border-radius: 5px; min-height: 150px;">
         <p style="text-align:center; color:#aaa;">Loading casters...</p>
      </div>
    </div>

    <!-- NEW: System Tools and Network Status -->
    <div class="box" style="max-width: 800px; margin-top: 20px;">
      <h2>System Tools & Status</h2>
      <div style="background: #444; padding: 15px; border-radius: 5px; text-align: left; margin-bottom: 20px;">
        <p style="margin: 5px 0;"><b>IP Address:</b> %IP%</p>
        <p style="margin: 5px 0;"><b>MAC Address:</b> %MAC%</p>
        <p style="margin: 5px 0;"><b>WiFi Signal:</b> %RSSI% dBm</p>
      </div>
      <div style="display: flex; gap: 10px; justify-content: center;">
        <a href="/sniffer" class="btn-green" style="text-decoration:none; padding:12px 20px; border-radius:5px;">IR Sniffer</a>
        <a href="/edge" class="btn-purple" style="text-decoration:none; padding:12px 20px; border-radius:5px;">Batuu Beacons</a>
      </div>
    </div>

    <div class="box" style="max-width: 800px; margin-top: 20px;">
      <h2>Network Settings</h2>
      <form action="/setwifi" method="GET">
        <input type="text" name="ssid" placeholder="New Wi-Fi Name" required><br>
        <input type="text" name="pass" placeholder="New Password" required><br>
        <button type="submit" class="btn-green" style="width: 90%;">Save & Reboot</button>
      </form>
    </div>

  </div>

  <script>
    function fetchCasters() {
      document.getElementById('casterWorkspace').innerHTML = '<p style="text-align:center; color:#aaa;">Fetching from database...</p>';
      fetch('/api/casters').then(r => r.text()).then(html => {
        document.getElementById('casterWorkspace').innerHTML = html;
      });
    }

    function showNewCasterForm() {
       let html = '<h3 style="margin-top:0;">Pair New Caster</h3>';
       html += '<p style="font-size: 0.9rem; color:#ccc;">Enter details below, click Start, then flick the wand.</p>';
       html += '<form action="/start_pairing" method="GET">';
       html += '<input type="text" name="caster" placeholder="Caster Name (e.g. Merlin)" required style="width:95%;"><br>';
       html += '<input type="text" name="story" placeholder="Story Name (e.g. quest1)" style="width:95%;"><br>';
       html += '<button type="submit" class="btn-purple">Start Pairing Mode</button>';
       html += '</form>';
       document.getElementById('casterWorkspace').innerHTML = html;
    }

    function updateCaster(hex) {
       let name = document.getElementById('name_' + hex).value;
       let story = document.getElementById('story_' + hex).value;
       let page = document.getElementById('page_' + hex).value;
       
       fetch('/api/update_caster', {
          method: 'POST', 
          headers: {'Content-Type': 'application/x-www-form-urlencoded'},
          body: `hex=${hex}&name=${encodeURIComponent(name)}&story=${encodeURIComponent(story)}&page=${page}`
       }).then(r => r.text()).then(msg => { 
          alert(msg); 
       });
    }

    function deleteCaster(hex) {
       if(confirm('Are you sure you want to delete this caster? Their story progress will be lost.')) {
         fetch('/api/delete_caster', {
            method: 'POST', 
            headers: {'Content-Type': 'application/x-www-form-urlencoded'},
            body: `hex=${hex}`
         }).then(r => r.text()).then(msg => { 
            alert(msg); 
            fetchCasters(); 
         });
       }
    }
  </script>
</body>
</html>
)rawliteral";

// --- Secret Author Webpage (LittleFS UI) ---
const char author_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Author Panel - MagiQuest</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  %DASHBOARD_CSS%
</head>
<body onload="fetchList()">
  
  <div class="sidebar">
    <h2>DASHBOARD</h2>
    <a href="/">Main (Story)</a>
    <a href="/author" class="active">Author Panel</a>
    <a href="/admin">Admin Panel</a>
  </div>

  <div class="content">
    <div class="box" style="max-width: 800px;">
      <h2>Author Panel: Story Manager</h2>
      
      <div style="display: flex; gap: 10px; justify-content: center; margin-bottom: 20px;">
        <button onclick="renderEditor('')" class="btn-purple">New Story</button>
        <button onclick="formatFS()" class="btn-red">Format Drive</button>
      </div>

      <div id="authorWorkspace" style="text-align: left; background: #444; padding: 20px; border-radius: 5px; min-height: 200px;">
         <p style="text-align:center; color:#aaa;">Loading stories...</p>
      </div>
    </div>
  </div>

  <script>
    function fetchList() {
      document.getElementById('authorWorkspace').innerHTML = '<p style="text-align:center; color:#aaa;">Fetching from LittleFS...</p>';
      fetch('/api/stories').then(r => r.text()).then(html => {
        document.getElementById('authorWorkspace').innerHTML = '<h3 style="margin-top:0;">Stored Stories</h3>' + html;
      });
    }

    function formatFS() {
      if(confirm('WARNING: This will permanently erase ALL stories! Are you sure?')) {
        document.getElementById('authorWorkspace').innerHTML = '<p style="text-align:center; color:#ffeb3b;">Formatting drive... Please wait.</p>';
        fetch('/api/format', {method: 'POST'}).then(r => r.text()).then(msg => { alert(msg); fetchList(); });
      }
    }

    function deleteStory(name) {
      if(confirm('Delete story: ' + name + '?')) {
        fetch('/api/delete?name=' + name, {method: 'POST'}).then(r => r.text()).then(msg => { alert(msg); fetchList(); });
      }
    }

    function editStory(name) {
       document.getElementById('authorWorkspace').innerHTML = '<p style="text-align:center; color:#aaa;">Loading ' + name + '...</p>';
       fetch('/api/story?name=' + name).then(r => r.text()).then(data => {
          renderEditor(name, data);
       });
    }

    function downloadStory(name) {
       fetch('/api/story?name=' + name).then(r => r.text()).then(data => {
          const blob = new Blob([data], { type: 'text/csv' });
          const url = window.URL.createObjectURL(blob);
          const a = document.createElement('a');
          a.style.display = 'none';
          a.href = url;
          a.download = name + '.csv';
          document.body.appendChild(a);
          a.click();
          window.URL.revokeObjectURL(url);
       });
    }

    // --- Dynamic Editor Logic ---
    function renderEditor(name, csvContent = "") {
      let html = '<h3 style="margin-top:0;">' + (name ? 'Editing: ' + name : 'Create New Story') + '</h3>';
      
      if (!name) {
        html += '<input type="text" id="storyName" placeholder="Story Name (e.g. quest1)" style="width:95%;"><br><br>';
      } else {
        html += '<input type="hidden" id="storyName" value="' + name + '">';
      }
      
      html += '<div id="rowsContainer"></div>';
      html += '<button onclick="addRow(\'\', \'\')" class="btn-purple btn-small" style="margin-top:15px;">+ Add Page</button>';
      html += '<button onclick="saveStory()" class="btn-green" style="margin-top:15px; margin-left:10px;">Save Story</button>';
      
      document.getElementById('authorWorkspace').innerHTML = html;

      if (csvContent) {
        // FIXED: Using single slash for actual newline character
        let lines = csvContent.split('\n');
        lines.forEach(line => {
          let parts = line.trim().split(',');
          if (parts.length >= 2) { 
            // Handle descriptions safely if they exist
            let desc = parts.length >= 3 ? parts[2] : ""; 
            addRow(parts[1], desc); 
          }
        });
      } else {
        addRow('', ''); 
      }
    }

    function addRow(ytId, desc) {
      let container = document.getElementById('rowsContainer');
      let row = document.createElement('div');
      row.className = 'story-row';
      row.innerHTML = `
        <span class="page-badge"></span>
        <input type="text" class="yt-id" placeholder="YouTube ID" value="${ytId}" style="width:130px;">
        <input type="text" class="desc" maxlength="40" placeholder="Description (Max 40 chars)" value="${desc}" style="flex:1;">
        <button onclick="this.parentElement.remove(); updatePageNumbers();" class="btn-red btn-small">X</button>
      `;
      container.appendChild(row);
      updatePageNumbers();
    }

    function updatePageNumbers() {
      let rows = document.querySelectorAll('.story-row');
      rows.forEach((row, index) => {
        row.querySelector('.page-badge').innerText = index + 1;
      });
    }

    function saveStory() {
       let name = document.getElementById('storyName').value.trim();
       if(name === "") { alert("Story name required!"); return; }

       let csvData = [];
       let rows = document.querySelectorAll('.story-row');
       rows.forEach((row, index) => {
          let pageNum = index + 1;
          let ytId = row.querySelector('.yt-id').value.trim();
          let desc = row.querySelector('.desc').value.trim();
          
          desc = desc.replace(/,/g, ''); 
          
          if(ytId) csvData.push(`${pageNum},${ytId},${desc}`);
       });

       if(csvData.length === 0) { alert("You need at least one valid page!"); return; }

       fetch('/api/story?name=' + name, {
          method: 'POST', 
          headers: {'Content-Type': 'application/x-www-form-urlencoded'},
          // FIXED: Using single slash for actual newline character
          body: 'data=' + encodeURIComponent(csvData.join('\n'))
       }).then(r => r.text()).then(msg => { 
          alert(msg); 
          fetchList(); 
       });
    }
  </script>
</body>
</html>
)rawliteral";

// --- Galaxy's Edge Webpage ---
const char edge_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Batuu Beacons - MagiQuest</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  %DASHBOARD_CSS%
</head>
<body>
  <div class="sidebar">
    <h2>DASHBOARD</h2>
    <a href="/">Main (Story)</a>
    <a href="/author">Author Panel</a>
    <a href="/admin">Admin Panel</a>
    <a href="/edge" class="active">Batuu Beacons</a>
  </div>
  
  <div class="content">
    <div class="box" style="max-width: 600px;">
      <h2>Galaxy's Edge Beacon Control</h2>
      <form action="/edge" method="GET">
        <h3 style="text-align: left; margin-top: 20px;">Select Active Location:</h3>
        <select name="preset" style="width: 100%; padding: 12px; margin-bottom: 20px; background: #444; color: white; border: 1px solid #555; border-radius: 5px; font-size: 1.1rem;">
          <option value="1" %SEL_1%>Market / Spaceport / Black Spire</option>
          <option value="2" %SEL_2%>First Order Outpost</option>
          <option value="3" %SEL_3%>Resistance Base</option>
          <option value="4" %SEL_4%>Scrap Area</option>
          <option value="5" %SEL_5%>Droid Depot</option>
        </select>
        <button type="submit" class="btn-purple" style="width: 100%;">TRANSMIT BEACON DATA</button>
      </form>
    </div>
  </div>
</body>
</html>
)rawliteral";

// --- Game Master CSV Reader ---
String getVideoForPage(String storyName, int targetPage) {
  // Ensure formatting is perfect for LittleFS
  if (!storyName.startsWith("/")) storyName = "/" + storyName;
  if (!storyName.endsWith(".csv")) storyName += ".csv";

  Serial.println("\n[GameMaster] Looking for file: " + storyName + " | Target Page: " + String(targetPage));

  File f = LittleFS.open(storyName, "r");
  if (!f) {
    Serial.println("[GameMaster] ERROR: File does not exist on the drive!");
    return "EOF"; 
  }

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim(); // Clean up invisible line breaks
    if (line.length() == 0) continue;

    Serial.println("[GameMaster] Parsing line: '" + line + "'");

    int firstComma = line.indexOf(',');
    int secondComma = line.indexOf(',', firstComma + 1);

    // Fallback: If the file only has "Page,VideoID" and is missing the description comma
    if (firstComma != -1 && secondComma == -1) {
      secondComma = line.length(); 
    }

    if (firstComma != -1) {
      int pageNum = line.substring(0, firstComma).toInt();
      if (pageNum == targetPage) {
        f.close();
        String foundId = line.substring(firstComma + 1, secondComma);
        Serial.println("[GameMaster] MATCH FOUND! Video ID: " + foundId);
        return foundId; 
      }
    }
  }
  
  f.close();
  Serial.println("[GameMaster] ERROR: Reached end of file without finding page " + String(targetPage));
  return "EOF"; 
}

// --- Core Game Logic ---
void processSpell(String hexStr, String nodeID, int magnitude) {
  hexStr.toUpperCase();
  
  if (isPairingMode) {
    // ... (Keep existing pairing logic exactly the same) ...
    wandPrefs.putString(hexStr.c_str(), pendingCasterName);
    wandPrefs.putString((hexStr + "_story").c_str(), pendingStoryName); 
    wandPrefs.putInt((hexStr + "_page").c_str(), 1); 

    String wandList = wandPrefs.getString("wandList", "");
    if (wandList.indexOf(hexStr) == -1) {
      wandPrefs.putString("wandList", wandList + hexStr + ",");
    }
    
    isPairingMode = false; 
    Serial.println("\n*** WAND PAIRED! ***");
    Serial.println("Caster: " + pendingCasterName + " | Story: " + pendingStoryName);
  } else {
    // --- NORMAL GAMEPLAY MODE ---
    String casterName = wandPrefs.getString(hexStr.c_str(), "Unknown Magi");
    
    String wandList = wandPrefs.getString("wandList", "");
    if (casterName != "Unknown Magi" && wandList.indexOf(hexStr) == -1) {
      wandPrefs.putString("wandList", wandList + hexStr + ",");
    }

    String storyName = wandPrefs.getString((hexStr + "_story").c_str(), "");
    int currentPage = wandPrefs.getInt((hexStr + "_page").c_str(), 1);
    
    // --> NEW: Log the context of the spell! <--
    Serial.print("Spell Cast! Caster: " + casterName);
    Serial.print(" | Target: " + nodeID);
    Serial.print(" | Power: " + String(magnitude));
    Serial.println(" | Story: " + (storyName == "" ? "None" : storyName) + " | Page: " + String(currentPage));

    String youtubeVideoId = "UNKNOWN";

    if (storyName != "") {
      youtubeVideoId = getVideoForPage(storyName, currentPage);

      if (youtubeVideoId == "EOF") {
        currentPage = 1;
        wandPrefs.putInt((hexStr + "_page").c_str(), 1);
        youtubeVideoId = getVideoForPage(storyName, currentPage);
      }

      if (youtubeVideoId != "EOF" && youtubeVideoId != "UNKNOWN") {
        wandPrefs.putInt((hexStr + "_page").c_str(), currentPage + 1);
      }
    } else {
      if (hexStr == "9235E81") youtubeVideoId = "fP-CJTtLYEg"; 
    }

    lastPayload = hexStr + "," + youtubeVideoId + "," + casterName;
    Serial.println("Payload ready: " + lastPayload);
  }
}

void updateBLEBeacon() {
  if (!pAdvertising) return;

  // Base Array: Manufacturer (83 01), Type (0A), Length (04), Payload (4 bytes)
  uint8_t gePayload[8] = { 0x83, 0x01, 0x0A, 0x04, 0x00, 0x00, 0x00, 0x00 };

  // Inject the exact 4-byte authentic Disney payloads based on the selected preset
  switch(activeBeaconPreset) {
    case 1: // Market / Spaceport / Black Spire
      gePayload[4] = 0x01; gePayload[5] = 0x02; gePayload[6] = 0xA6; gePayload[7] = 0x01; break;
    case 2: // First Order Outpost
      gePayload[4] = 0x01; gePayload[5] = 0x01; gePayload[6] = 0xA6; gePayload[7] = 0x01; break;
    case 3: // Resistance Base
      gePayload[4] = 0x03; gePayload[5] = 0x02; gePayload[6] = 0xA6; gePayload[7] = 0x01; break;
    case 4: // Scrap Area
      gePayload[4] = 0x01; gePayload[5] = 0x03; gePayload[6] = 0xA6; gePayload[7] = 0x01; break;
    case 5: // Droid Depot
      gePayload[4] = 0x02; gePayload[5] = 0x02; gePayload[6] = 0xA6; gePayload[7] = 0x01; break;
  }

  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
  oAdvertisementData.setManufacturerData(String((char*)gePayload, sizeof(gePayload)));
  pAdvertising->setAdvertisementData(oAdvertisementData);
  
  // Restart the beacon to apply changes
  pAdvertising->stop();
  delay(10);
  pAdvertising->start();
  
  Serial.printf("[BLE] Beacon Updated to Disney Preset %d\n", activeBeaconPreset);
}

void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS Mount Failed. It may need to be formatted via the Author Panel.");
  } else {
    Serial.println("LittleFS Mounted Successfully.");
  }

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  Serial.println("IR Receiver Started on Pin 9");

  preferences.begin("wifi-creds", false);
  wandPrefs.begin("wands", false); 
  
  currentSSID = preferences.getString("ssid", "Default_Hotspot");
  currentPassword = preferences.getString("pass", "Default_Password");

  Serial.print("Connecting to saved Wi-Fi: ");
  Serial.print(currentSSID);
  Serial.print("/");
  Serial.println(currentPassword);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(currentSSID.c_str(), currentPassword.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 15) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  Serial.println(); 

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Success! Open this IP to play: http://");
    Serial.println(WiFi.localIP());
    Serial.print("Dashboard Panel: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/admin");
  } else {
    Serial.println("Connection Failed! Starting Emergency Access Point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("MagiQuest-Admin", "magic123"); 
    Serial.print("Connect phone to 'MagiQuest-Admin' (Pass: magic123) and go to: http://");
    Serial.print(WiFi.softAPIP());
    Serial.println("/admin");
  }
  
  // ---------------------------------------------------------
  // START GALAXY'S EDGE BLE BEACON
  // ---------------------------------------------------------
  // Load Galaxy's Edge preset from memory
  blePrefs.begin("ge_beacon", false); 
  activeBeaconPreset = blePrefs.getUChar("preset", 1);

  Serial.println("[BLE] Starting Galaxy's Edge Beacon...");
  BLEDevice::init("MagiQuest-GE-Node");
  pAdvertising = BLEDevice::getAdvertising();
  
  // Call the update function to set the initial payload and start broadcasting
  updateBLEBeacon();

  // --- Core Web Server Routes ---
  server.on("/", []() {
    server.send(200, "text/html", index_html);
  });

  server.on("/wand", []() {
    server.send(200, "text/plain", lastPayload);
    lastPayload = ""; 
  });

server.on("/admin", []() {
    String html = String(admin_html);
    
    // Inject the CSS and Live Data
    html.replace("%DASHBOARD_CSS%", dashboard_css);
    html.replace("%IP%", WiFi.localIP().toString());
    html.replace("%MAC%", WiFi.macAddress());
    html.replace("%RSSI%", String(WiFi.RSSI()));
    
    server.send(200, "text/html", html);
  });

  server.on("/author", []() {
    String html = String(author_html);
    html.replace("%DASHBOARD_CSS%", dashboard_css);
    server.send(200, "text/html", html);
  });

  server.on("/edge", []() {
    // Process form submission and save to preferences
    if (server.hasArg("preset")) {
      activeBeaconPreset = (uint8_t) server.arg("preset").toInt();
      blePrefs.putUChar("preset", activeBeaconPreset);
      updateBLEBeacon();
      
      // Clean redirect to avoid duplicate submissions on page refresh
      server.sendHeader("Location", "/edge");
      server.send(303);
      return;
    }

    String html = String(edge_html);
    html.replace("%DASHBOARD_CSS%", dashboard_css);
    
    // Dynamically add the "selected" attribute to the correct dropdown option
    for (int i = 1; i <= 5; i++) {
      String placeholder = "%SEL_" + String(i) + "%";
      if (activeBeaconPreset == i) {
         html.replace(placeholder, "selected");
      } else {
         html.replace(placeholder, "");
      }
    }
    
    server.send(200, "text/html", html);
  });

  server.on("/setwifi", []() {
    String newSSID = server.arg("ssid");
    String newPass = server.arg("pass");
    preferences.putString("ssid", newSSID);
    preferences.putString("pass", newPass);
    server.send(200, "text/plain", "Credentials Saved! The ESP32 is rebooting now. Connect to your normal Wi-Fi network.");
    delay(1500);
    ESP.restart(); 
  });

// --- Pairing Mode Routes ---
  server.on("/start_pairing", []() {
    pendingCasterName = server.arg("caster");
    pendingStoryName = server.arg("story"); 
    
    // The Whitespace Assassin: Clean the inputs before saving!
    pendingCasterName.trim();
    pendingStoryName.trim();
    
    isPairingMode = true;
    pairingStartTime = millis();
    server.sendHeader("Location", "/pairing_status");
    server.send(303);
  });

  server.on("/pairing_status", []() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    if (isPairingMode) {
      html += "<meta http-equiv='refresh' content='2'>";
      html += "<style>body{background:#222; color:#fff; font-family:sans-serif; text-align:center; padding-top:50px;} .loader{border:8px solid #333; border-top:8px solid #9c27b0; border-radius:50%; width:50px; height:50px; animation:spin 2s linear infinite; margin:20px auto;} @keyframes spin {0%{transform:rotate(0deg);} 100%{transform:rotate(360deg);}}</style>";
      html += "</head><body><h2>Pairing Mode Active</h2><p>Flick <b>" + pendingCasterName + "'s</b> wand at the crystal now...</p>";
      html += "<div class='loader'></div><p><a href='/admin' style='color:#ccc;'>Cancel</a></p></body></html>";
    } else {
      html += "<style>body{background:#4caf50; color:#fff; font-family:sans-serif; text-align:center; padding-top:50px;} a{color:#fff; text-decoration:underline;}</style>";
      html += "</head><body><h2>Success!</h2><p>Wand successfully paired to <b>" + pendingCasterName + "</b>.</p><br><br><a href='/admin'>Back to Admin</a> | <a href='/author'>Go to Author Panel</a></body></html>";
    }
    server.send(200, "text/html", html);
  });

  // --- Caster Management API Endpoints ---
  server.on("/api/casters", HTTP_GET, []() {
    String wandStr = wandPrefs.getString("wandList", "");
    if (wandStr == "") {
      server.send(200, "text/html", "<p style='color:#aaa;'>No casters registered yet.</p>");
      return;
    }

    String html = "";
    int start = 0;
    int end = wandStr.indexOf(',');
    
    while (end != -1) {
      String hex = wandStr.substring(start, end);
      String name = wandPrefs.getString(hex.c_str(), "Unknown");
      String story = wandPrefs.getString((hex + "_story").c_str(), "");
      int page = wandPrefs.getInt((hex + "_page").c_str(), 1);

      html += "<div class='story-row' style='flex-wrap: wrap; margin-bottom: 15px;'>";
      html += "<strong style='width:100%; color:#9c27b0;'>Wand ID: " + hex + "</strong>";
      html += "<input type='text' id='name_" + hex + "' value='" + name + "' placeholder='Caster Name' style='flex:1; min-width:120px;'>";
      html += "<input type='text' id='story_" + hex + "' value='" + story + "' placeholder='Story (e.g. quest1)' style='flex:1; min-width:120px;'>";
      html += "<input type='number' id='page_" + hex + "' value='" + String(page) + "' style='width:70px;' title='Page Number'>";
      html += "<button onclick='updateCaster(\"" + hex + "\")' class='btn-green btn-small'>Save</button>";
      html += "<button onclick='deleteCaster(\"" + hex + "\")' class='btn-red btn-small'>Delete</button>";
      html += "</div>";

      start = end + 1;
      end = wandStr.indexOf(',', start);
    }
    server.send(200, "text/html", html);
  });

server.on("/api/update_caster", HTTP_POST, []() {
    String hex = server.arg("hex");
    String newName = server.arg("name");
    String newStory = server.arg("story");
    
    // Clean the inputs from the Admin Panel
    newName.trim();
    newStory.trim();
    
    wandPrefs.putString(hex.c_str(), newName);
    wandPrefs.putString((hex + "_story").c_str(), newStory);
    wandPrefs.putInt((hex + "_page").c_str(), server.arg("page").toInt());
    
    server.send(200, "text/plain", "Caster Updated Successfully!");
  });

  server.on("/api/delete_caster", HTTP_POST, []() {
    String hex = server.arg("hex");
    
    // 1. Remove from index list
    String wandStr = wandPrefs.getString("wandList", "");
    wandStr.replace(hex + ",", ""); // Snip it out of the comma-separated list
    wandPrefs.putString("wandList", wandStr);

    // 2. Erase their personal data
    wandPrefs.remove(hex.c_str());
    wandPrefs.remove((hex + "_story").c_str());
    wandPrefs.remove((hex + "_page").c_str());
    
    server.send(200, "text/plain", "Caster Deleted.");
  });

  // --- LittleFS API Endpoints (For the Author Panel) ---
  // 1. List Files
  server.on("/api/stories", []() {
    String html = "<ul style='list-style:none; padding:0;'>";
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    bool hasFiles = false;
    
    while(file){
      hasFiles = true;
      String fName = String(file.name());
      
      // Clean up slash and extension for UI Display
      if(fName.startsWith("/")) fName = fName.substring(1); 
      if(fName.endsWith(".csv")) fName = fName.substring(0, fName.length() - 4);
      
      html += "<li style='margin-bottom:10px; background:#333; padding:15px; border-radius:5px; display:flex; justify-content:space-between; align-items:center;'>";
      html += "<strong style='font-size:1.1rem;'>" + fName + "</strong>";
      
      // Added Download (DL) button here!
      html += "<div>";
      html += "<button onclick='downloadStory(\"" + fName + "\")' class='btn-purple btn-small'>DL</button>";
      html += "<button onclick='editStory(\"" + fName + "\")' class='btn-green btn-small'>Edit</button>";
      html += "<button onclick='deleteStory(\"" + fName + "\")' class='btn-red btn-small'>Delete</button>";
      html += "</div></li>";
      
      file = root.openNextFile();
    }
    if(!hasFiles) html += "<li style='color:#aaa;'>No stories found on drive. Create a new one above!</li>";
    html += "</ul>";
    
    server.send(200, "text/html", html);
  });
  // 2. Format Drive
  server.on("/api/format", HTTP_POST, []() {
    if(LittleFS.format()){
      server.send(200, "text/plain", "SUCCESS: Drive formatted and wiped clean.");
    } else {
      server.send(500, "text/plain", "ERROR: Formatting failed.");
    }
  });

  // 3. Delete File
  server.on("/api/delete", HTTP_POST, []() {
    String name = server.arg("name");
    if(!name.startsWith("/")) name = "/" + name; 
    if(!name.endsWith(".csv")) name += ".csv"; 

    if(LittleFS.remove(name)){
      server.send(200, "text/plain", "Deleted!");
    } else {
      server.send(500, "text/plain", "ERROR: Could not delete file.");
    }
  });

  // 4. Get File Contents (For Editing)
  server.on("/api/story", HTTP_GET, []() {
    String name = server.arg("name");
    if(!name.startsWith("/")) name = "/" + name; 
    if(!name.endsWith(".csv")) name += ".csv"; 

    File f = LittleFS.open(name, "r");
    if(!f){
      server.send(404, "text/plain", "");
      return;
    }
    String content = f.readString();
    f.close();
    server.send(200, "text/plain", content);
  });

  // 5. Save/Overwrite File
  server.on("/api/story", HTTP_POST, []() {
    String name = server.arg("name");
    if(!name.startsWith("/")) name = "/" + name; 
    if(!name.endsWith(".csv")) name += ".csv"; 

    String data = server.arg("data");
    File f = LittleFS.open(name, "w");
    if(!f){
      server.send(500, "text/plain", "ERROR: Failed to open file for writing.");
      return;
    }
    f.print(data);
    f.close();
    server.send(200, "text/plain", "SUCCESS: Story saved!");
  });

  // --- Remote Satellite Node Endpoint ---
  server.on("/api/remote_cast", HTTP_GET, []() {
    String hex = server.arg("hex");
    String node = server.arg("node");
    if (node == "") node = "remote_unknown"; // Fallback if satellite forgets to ID itself
    
    int mag = server.arg("mag").toInt();
    if (mag == 0) mag = 1; // Fallback default power

    if (hex != "") {
      Serial.println("[Network] Received spell from satellite: " + node);
      processSpell(hex, node, mag);
      server.send(200, "text/plain", "Spell Accepted by Master");
    } else {
      server.send(400, "text/plain", "Missing Hex ID");
    }
  });

  server.on("/sniffer", []() {
    String html = "<html><head><title>Server IR Sniffer</title>";
    html += "<meta http-equiv='refresh' content='2'>"; // Auto-refresh every 2s
    html += "<style>body { background-color: #111; color: #00ffcc; font-family: monospace; text-align: center; margin-top: 10%; }</style>";
    html += "</head><body>";
    html += "<h2>MagicQuestServer Diagnostic Sniffer</h2>";
    html += "<h1>Last IR Code Captured:</h1>";
    html += "<p style='font-size: 4rem; border: 2px solid #00ffcc; display: inline-block; padding: 20px; border-radius: 15px; margin: 10px;'>" + lastSniffedHex + "</p>";
    html += "<h3>Protocol Identified: <span style='color: #ffeb3b;'>" + lastSniffedProtocol + "</span></h3>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
  });

    server.begin();
}

void loop() {
  server.handleClient();

  if (isPairingMode && (millis() - pairingStartTime > PAIRING_TIMEOUT)) {
    isPairingMode = false;
    Serial.println("Pairing mode timed out.");
  }

if (IrReceiver.decode()) {
    // ---------------------------------------------------------
    // STEP A: Feed the Sniffer (Catch EVERYTHING)
    // ---------------------------------------------------------
    // Get the protocol name (IRremote v3/v4 has a built-in helper for this)
    lastSniffedProtocol = String(getProtocolString(IrReceiver.decodedIRData.protocol));
    
    // Get the raw hex data
    lastSniffedHex = String(IrReceiver.decodedIRData.decodedRawData, HEX);
    lastSniffedHex.toUpperCase();
    
    // Optional: Print everything to the Master's Serial Monitor too
    Serial.println("[Sniffer] Protocol: " + lastSniffedProtocol + " | Hex: " + lastSniffedHex);


    // ---------------------------------------------------------
    // STEP B: The Actual Game Engine (Filter for MagiQuest ONLY)
    // ---------------------------------------------------------
    if (IrReceiver.decodedIRData.protocol == MAGIQUEST) {
      // Your existing game logic goes here!
      // Example: check wand ID against LittleFS story CSVs, 
      // update Preferences, trigger WebSocket video change, etc.
      
      Serial.println("[Game] Valid Spell Cast by Wand: " + lastSniffedHex);
    }

    // Resume listening for the next burst
    IrReceiver.resume();
  }
}