#pragma once

// index.h

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
<title>Petra Aquaponics LoRaWAN Sensor</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f4; color: #333; margin: 0; padding: 0; }
  .container { max-width: 500px; margin: 20px auto; padding: 20px; background-color: #fff; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
  h2 { color: #0056b3; }
  input[type=text], input[type=number] { width: calc(100% - 24px); padding: 12px; margin: 8px 0; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
  input[type=submit] { background-color: #007bff; color: white; padding: 14px 20px; margin: 8px 0; border: none; border-radius: 4px; cursor: pointer; width: 100%; font-size: 16px; }
  input[type=submit]:hover { background-color: #0056b3; }
  input[type=submit]:disabled { background-color: #cccccc; cursor: not-allowed; }
  .status { margin-top: 15px; font-size: 1.1em; font-weight: bold; min-height: 20px; }
  .success { color: #28a745; }
  .error { color: #dc3545; }
  hr { border: 0; height: 1px; background: #ddd; margin: 25px 0; }
  details { border: 1px solid #ddd; border-radius: 4px; margin-top: 20px; }
  summary { font-weight: bold; cursor: pointer; padding: 12px; background-color: #f9f9f9; border-radius: 4px 4px 0 0; }
  .details-content { padding: 15px; }
  .toggle-container { display: flex; align-items: center; justify-content: space-between; margin: 15px 0; }
</style>
</head><body>
<div class="container">
  <h2>LoRa Message Sender</h2>
  <form id="sendForm">
    <input type="text" id="message" name="message" placeholder="Enter message to send" required>
    <input type="submit" id="sendButton" value="Send Message">
  </form>
  <div id="status" class="status"></div>
  <hr>
  <h2>Update Display Title</h2>
  <form id="titleForm">
    <input type="text" id="newTitle" name="title" placeholder="New title (max 10 chars)" maxlength="10" required>
    <input type="submit" id="titleButton" value="Update Title">
  </form>
  <div id="titleStatus" class="status"></div>

  <details>
    <summary>Advanced Settings</summary>
    <div class="details-content">
      <div class="toggle-container">
        <label for="tempToggle">Use Live Temperature Reading for DO</label>
        <input type="checkbox" id="tempToggle">
      </div>
      <form id="defaultTempForm" style="display: none;">
        <input type="number" id="newDefaultTemp" name="defaultTemp" placeholder="Default water temp (0-40°C)" min="0" max="40" step="1" required>
        <input type="submit" id="defaultTempButton" value="Set Default Temperature">
      </form>
      <div id="tempStatus" class="status"></div>
      <hr>
      <h2>Update Uplink Interval</h2>
      <form id="intervalForm">
        <input type="number" id="newInterval" name="interval" placeholder="New interval in seconds (min 90)" min="90" required>
        <input type="submit" id="intervalButton" value="Update Interval">
      </form>
      <div id="intervalStatus" class="status"></div>
      <hr>
      <h2>Update Gas Sensor Ro</h2>
      <form id="roForm">
        <input type="number" id="newRo" name="ro" placeholder="New Ro value (e.g., 30000)" step="any" required>
        <input type="submit" id="roButton" value="Update Ro">
      </form>
      <div id="roStatus" class="status"></div>
    </div>
  </details>
</div>
<script>
  // Generic function to handle form submissions
  function handleFormSubmit(formId, url, statusDivId, buttonId) {
    const form = document.getElementById(formId);
    const statusDiv = document.getElementById(statusDivId);
    const button = document.getElementById(buttonId);

    form.addEventListener('submit', function(e) {
      e.preventDefault();
      const formData = new URLSearchParams(new FormData(form)).toString();
      
      button.disabled = true;
      statusDiv.className = 'status';
      statusDiv.textContent = 'Updating...';

      fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: formData
      })
      .then(response => response.text().then(text => ({ ok: response.ok, text })))
      .then(({ ok, text }) => {
        statusDiv.className = ok ? 'status success' : 'status error';
        statusDiv.textContent = text;
        button.disabled = false;
      })
      .catch(error => {
        statusDiv.className = 'status error';
        statusDiv.textContent = 'Error: Request failed.';
        button.disabled = false;
      });
    });
  }

  // Handle LoRa message sending with status polling
  const sendForm = document.getElementById('sendForm');
  const sendButton = document.getElementById('sendButton');
  const statusDiv = document.getElementById('status');
  let pollingInterval;

  sendForm.addEventListener('submit', function(e) {
    e.preventDefault();
    const message = document.getElementById('message').value;
    
    sendButton.disabled = true;
    statusDiv.className = 'status';
    statusDiv.textContent = 'Sending...';

    fetch('/send', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'message=' + encodeURIComponent(message)
    })
    .then(response => {
      if (response.ok) {
        pollingInterval = setInterval(checkStatus, 1000);
      } else { throw new Error('Server error.'); }
    })
    .catch(error => {
      statusDiv.className = 'status error';
      statusDiv.textContent = 'Error: Could not send message.';
      sendButton.disabled = false;
    });
  });

  function checkStatus() {
    fetch('/status')
      .then(response => response.text())
      .then(statusText => {
        if (statusText === 'SUCCESS') {
          clearInterval(pollingInterval);
          statusDiv.className = 'status success';
          statusDiv.textContent = 'Message Sent Successfully!';
          sendButton.disabled = false;
        } else if (statusText === 'FAILED') {
          clearInterval(pollingInterval);
          statusDiv.className = 'status error';
          statusDiv.textContent = 'Failed: Message could not be sent.';
          sendButton.disabled = false;
        } else if (statusText === 'SENDING') {
          statusDiv.textContent = 'Waiting for confirmation...';
        }
      })
      .catch(error => {
        clearInterval(pollingInterval);
        statusDiv.className = 'status error';
        statusDiv.textContent = 'Error: Lost connection to server.';
        sendButton.disabled = false;
      });
  }

  // Temperature toggle logic
  const tempToggle = document.getElementById('tempToggle');
  const defaultTempForm = document.getElementById('defaultTempForm');
  const tempStatusDiv = document.getElementById('tempStatus');

  tempToggle.addEventListener('change', function() {
    const useLive = this.checked;
    defaultTempForm.style.display = useLive ? 'none' : 'block';
    
    tempStatusDiv.textContent = 'Updating...';
    fetch('/settogletemp', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'useLive=' + useLive
    })
    .then(response => response.text().then(text => ({ ok: response.ok, text })))
    .then(({ok, text}) => {
        tempStatusDiv.className = ok ? 'status success' : 'status error';
        tempStatusDiv.textContent = text;
    })
    .catch(error => {
        tempStatusDiv.className = 'status error';
        tempStatusDiv.textContent = 'Error: Request failed.';
    });
  });

  // Fetch initial settings on page load
  window.addEventListener('load', function() {
    fetch('/getsettings')
      .then(response => response.json())
      .then(data => {
        document.getElementById('newTitle').value = data.title;
        document.getElementById('newInterval').value = data.interval;
        document.getElementById('newRo').value = data.ro;
        document.getElementById('tempToggle').checked = data.useLiveTemp;
        document.getElementById('newDefaultTemp').value = data.defaultTemp;
        
        // Trigger change event to set initial UI state for temp form
        tempToggle.dispatchEvent(new Event('change'));
        // Clear the "Updating..." message from the toggle change
        setTimeout(() => { tempStatusDiv.textContent = ''; }, 500);
      })
      .catch(error => console.error('Error fetching settings:', error));
  });

  // Initialize all form handlers
  handleFormSubmit('titleForm', '/settitle', 'titleStatus', 'titleButton');
  handleFormSubmit('intervalForm', '/setinterval', 'intervalStatus', 'intervalButton');
  handleFormSubmit('roForm', '/setro', 'roStatus', 'roButton');
  handleFormSubmit('defaultTempForm', '/setdefaulttemp', 'tempStatus', 'defaultTempButton');

</script>
</body></html>)rawliteral";
