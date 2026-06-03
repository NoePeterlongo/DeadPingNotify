/**
 * Handles incoming pings from ESP32 devices (POST request)
 * Updates the timestamp, clears notification status, and alerts if it returns online.
 */
function doPost(e) {
  try {
    const sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
    const data = JSON.parse(e.postData.contents);

    const API_KEY = sheet.getRange(2, 2).getValue();
    
    // 1. Authenticate the request
    if (!data.api_key || data.api_key !== API_KEY) {
      return ContentService.createTextOutput("Error: Authentication failed.");
    }
    
    const deviceId = data.id;
    if (!deviceId) {
      return ContentService.createTextOutput("Error: Missing device identifier.");
    }
    
    // 2. Search for the device ID in the sheet (starting from row 5 / index 4)
    const rows = sheet.getDataRange().getValues();
    const serverTimestamp = new Date().getTime();
    let rowIndex = -1;
    
    for (let i = 4; i < rows.length; i++) {
      if (rows[i][0] === deviceId) {
        rowIndex = i + 1; // Convert 0-based index to 1-based row number
        break;
      }
    }
    
    const emailRow = rows[0];
    const recipients = [];
    for (let j = 1; j < emailRow.length; j++) {
      const email = emailRow[j].toString().trim();
      if (email && email.includes("@")) {
        recipients.push(email);
      }
    }
    const emailList = recipients.join(",");
    
    // 4. Update existing row or create a new one
    if (rowIndex !== -1) {
      const previousStatus = rows[rowIndex - 1][2]; 
      
      // Device found: Update timestamp (Col B) and clear notification status (Col C)
      sheet.getRange(rowIndex, 2).setValue(serverTimestamp);
      sheet.getRange(rowIndex, 3).setValue(""); 
      
      // SI le système était hors-ligne (notification envoyée), on signale son retour
      if (previousStatus === "Notification Envoyée" && emailList) {
        const formattedDate = new Date(serverTimestamp).toLocaleString("fr-FR");
        const subject = `🟢 Retour en ligne - ESP32 (${deviceId})`;
        const body = `L'ESP32 avec l'identifiant "${deviceId}" est de nouveau en ligne.\n\n` +
                     `Ping de reprise reçu le : ${formattedDate}\n` +
                     `Le système fonctionne à nouveau normalement.`;
        
        MailApp.sendEmail(emailList, subject, body);
      }
      
    } else {
      // Device not found: Append a new row [ID, Timestamp, Status]
      sheet.appendRow([deviceId, serverTimestamp, ""]);
    }
    
    return ContentService.createTextOutput("Ping processed successfully.");
  } catch (error) {
    return ContentService.createTextOutput("Error: " + error.toString());
  }
}

/**
 * Checks all devices and sends email notifications if a timeout is detected.
 * Designed to be triggered every minute.
 */
function checkDevicePings() {
  const sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
  const rows = sheet.getDataRange().getValues();
  
  if (rows.length < 5) return; // Au moins les lignes de config + les entêtes + 1 ligne de données
  
  // 1. Retrieve email recipients from Row 1 (starting from Column B / index 1)
  const emailRow = rows[0];
  const recipients = [];
  for (let j = 1; j < emailRow.length; j++) {
    const email = emailRow[j].toString().trim();
    if (email && email.includes("@")) {
      recipients.push(email);
    }
  }
  
  if (recipients.length === 0) {
    Logger.log("No valid email recipients found in row 1.");
    return; 
  }
  const emailList = recipients.join(","); // Comma-separated list for MailApp

  const TIMEOUT_S = sheet.getRange(3, 2).getValue();
  
  // 2. Check each device from Row 5 onwards
  const now = new Date().getTime();
  
  for (let i = 4; i < rows.length; i++) {
    const deviceId = rows[i][0];
    const lastPing = rows[i][1];
    const status = rows[i][2];
    
    // Validate that we have a device ID and a numeric timestamp
    if (deviceId && typeof lastPing === "number" && status !== "Notification Envoyée") {
      const ageS = (now - lastPing) / 1000;
      
      // If the device has stopped pinging (potential power outage)
      if (ageS > TIMEOUT_S) {
        const rowNumber = i + 1;
        const formattedDate = new Date(lastPing).toLocaleString("fr-FR");
        
        // Notification texts in French
        const subject = `⚠️ Coupure de courant suspectée - ESP32 (${deviceId})`;
        const body = `Attention, l'ESP32 avec l'identifiant "${deviceId}" n'envoie plus de pings.\n\n` +
                     `Dernier ping reçu le : ${formattedDate}\n` +
                     `Une coupure de courant est probablement en cours à cet emplacement.`;
        
        // Send email to all recipients
        MailApp.sendEmail(emailList, subject, body);
        
        // Update status to prevent duplicate emails
        sheet.getRange(rowNumber, 3).setValue("Notification Envoyée");
      }
    }
  }
}