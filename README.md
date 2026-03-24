<img width="1408" height="768" alt="image" src="https://github.com/user-attachments/assets/9366e1da-a7a3-4bbc-a1eb-43188f407235" />
<br/>
## **Setting up Firebase account**
<br/>1. Go to [Firebase console](https://console.firebase.google.com/u/0/)
<br/>2. Click on "Create a new Firebase Project"
<br/><img width="627" height="331" alt="image" src="https://github.com/user-attachments/assets/098bb577-cfa3-4a00-9ece-10785e5732ee" />
<br/>3. Give project name and click continue
<br/>4.  Next 2 steps as your wish (better to disable analytics for now)
<br/>5. Select "Databases & storage -> Realtime Database"
<br/><img width="875" height="645" alt="image" src="https://github.com/user-attachments/assets/782d522d-291a-4fb0-a67a-be29e85d19c1" />

<br/>6. Click on "Create Database" and choose to use Singapore Server as its the closest to India
<br/>7. For our educational purpose, we will start in "test mode"
<br/>8. Note the URL from the top,Its needed later, The highlighted one
<br/> <img width="1284" height="661" alt="image" src="https://github.com/user-attachments/assets/0327ad24-ce06-4349-bc5f-6b8747e59256" />

<br/>9. You have setup the Firebase Realtime Database with Full access to anyone who have access to this link :)

 ## **Hardware**
<br/>1. GPIO33 for powering LM35 [+Vs]
<br/>   GPIO34 for the analog Data in [VOUT from LM35]
<br/>   GND to GND
<br/>   <img width="981" height="787" alt="image" src="https://github.com/user-attachments/assets/d73d0756-b89f-4eb5-8fd0-e8da853c06a6" />
<br/><img width="1200" height="526" alt="image" src="https://github.com/user-attachments/assets/0a538283-1b54-45f8-b5a4-af7df44de5d9" />

<br/>2.<img width="1600" height="739" alt="image" src="https://github.com/user-attachments/assets/d7d33271-eada-425e-8279-fcda71f42fd9" />



## **Software**
<br/>Assuming ESP-IDF is already installed in VSCode, else refer to some other tutorial to do so :)
<br/>1. In VScode, Create a new ESP-IDF project by 
<br/>	1. "CTRL + SHIFT + P" -> ESP-IDF: New Project
<br/>	2. Preferred Framework version is V5.4.1.
<br/>	3.<br/> <img width="1131" height="248" alt="image" src="https://github.com/user-attachments/assets/bb841b8b-14b0-484f-95d8-0c0d1b3b9120" />
<br/>	4.<br/> <img width="816" height="554" alt="image" src="https://github.com/user-attachments/assets/33bb29e8-3cf3-448e-adff-2fec85db6b29" />
<br/>		Select the appropriate options and click "Create Project" -> "Open Project"
<br/>2. Open the "CMakeLists.txt" and modify
<br/>
```
   idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ".")
```
<br/>
to 
<br/>

```
   idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_wifi esp_http_client esp_adc json esp-tls driver)
```

<br/>
	This is done to link the required libraries to be linked and used while compiling the code
<!DOCTYPE html>
<html>
<head>
</head>
<body>

<table>
  <thead>
    <tr>
      <th>Component</th>
      <th>Function in the Project</th>
      <th>Why it is Essential</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>nvs_flash</code></td>
      <td><strong>Non-Volatile Storage</strong></td>
      <td>Stores Wi-Fi calibration data and credentials. Wi-Fi will usually fail to initialize without this.</td>
    </tr>
    <tr>
      <td><code>esp_wifi</code></td>
      <td><strong>Wi-Fi Stack</strong></td>
      <td>Provides the internal logic to scan for your SSID, perform the WPA2-AES handshake, and maintain a connection.</td>
    </tr>
    <tr>
      <td><code>esp_http_client</code></td>
      <td><strong>HTTP/HTTPS Engine</strong></td>
      <td>Handles the "REST API" calls. It formats the headers and manages the <code>PATCH</code> request to the Firebase URL.</td>
    </tr>
    <tr>
      <td><code>esp_tls</code></td>
      <td><strong>Transport Layer Security</strong></td>
      <td>The "Security" layer. It encrypts your data so it can be sent over <code>HTTPS</code> (port 443) instead of insecure <code>HTTP</code>.</td>
    </tr>
    <tr>
      <td><code>esp_crt_bundle</code></td>
      <td><strong>Certificate Authority</strong></td>
      <td>Contains the "Root Certificates" needed to verify that the Firebase server is authentic and safe to connect to.</td>
    </tr>
    <tr>
      <td><code>esp_adc</code></td>
      <td><strong>Analog-to-Digital Converter</strong></td>
      <td>The driver that reads the voltage from your <strong>LM35</strong> (0.01V per degree) and converts it into a digital number (0–4095).</td>
    </tr>
    <tr>
      <td><code>json</code> (cJSON)</td>
      <td><strong>Data Formatting</strong></td>
      <td>Formats your raw numbers into a string that Firebase understands, such as <code>{"temperature": 25.5}</code>.</td>
    </tr>
  </tbody>
</table>

</body>
</html>
<br/>3.<br/> <img width="824" height="601" alt="image" src="https://github.com/user-attachments/assets/839a94ca-c770-4467-afd8-016ec9b41c9a" />
<br/>4. Replace WIFI_SSID, WIFI_PASS and FIREBASE_URL in main.c to your credentials

<br/>5. Now you can see the Database getting updated in Firebase, VOILAA!!!
