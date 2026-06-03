/**
 * Handles incoming pings from ESP32 devices (POST request)
 * Updates the timestamp and clears notification status.
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
    
    // 2. Search for the device ID in the sheet (starting from row 3 / index 2)
    const rows = sheet.getDataRange().getValues();
    const serverTimestamp = new Date().getTime();
    let rowIndex = -1;
    
    for (let i = 2; i < rows.length; i++) {
      if (rows[i][0] === deviceId) {
        rowIndex = i + 1; // Convert 0-based index to 1-based row number
        break;
      }
    }
    
    // 3. Update existing row or create a new one
    if (rowIndex !== -1) {
      // Device found: Update timestamp (Col B) and clear notification status (Col C)
      sheet.getRange(rowIndex, 2).setValue(serverTimestamp);
      sheet.getRange(rowIndex, 3).setValue(""); 
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
  
  if (rows.length < 3) return; // Not enough data to process (needs at least config, headers, and 1 data row)
  
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

  const TIMEOUT_MS = sheet.getRange(3, 2).getValue();
  
  // 2. Check each device from Row 5 onwards
  const now = new Date().getTime();
  
  for (let i = 4; i < rows.length; i++) {
    const deviceId = rows[i][0];
    const lastPing = rows[i][1];
    const status = rows[i][2];
    
    // Validate that we have a device ID and a numeric timestamp
    if (deviceId && typeof lastPing === "number" && status !== "Notification Envoyée") {
      const ageMs = now - lastPing;
      
      // If the device has stopped pinging (potential power outage)
      if (ageMs > TIMEOUT_MS) {
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