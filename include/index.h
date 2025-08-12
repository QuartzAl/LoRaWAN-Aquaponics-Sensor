#pragma once

// index.h

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
<title>Petra Aquaponics Dissolved Oxygen and Air Quality LoRaWAN Sensor</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f4; color: #333; }
  .container { max-width: 500px; margin: 30px auto; padding: 20px; background-color: #fff; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
  h2 { color: #0056b3; }
  input[type=text], input[type=number] { width: calc(100% - 24px); padding: 12px; margin: 10px 0; border: 1px solid #ccc; border-radius: 4px; }
  input[type=submit] { background-color: #007bff; color: white; padding: 14px 20px; margin: 8px 0; border: none; border-radius: 4px; cursor: pointer; width: 100%; font-size: 16px; }
  input[type=submit]:hover { background-color: #0056b3; }
  input[type=submit]:disabled { background-color: #cccccc; }
  #status, #titleStatus, #intervalStatus, #roStatus, #waterTempStatus { margin-top: 15px; font-size: 1.1em; font-weight: bold; min-height: 20px;}
  .success { color: #28a745; }
  .error { color: #dc3545; }
  hr { border: 0; height: 1px; background: #ddd; margin: 30px 0; }
</style>
</head><body>
<div class="container">
  <h2>LoRa Message Sender</h2>
  <form id="sendForm">
    <input type="text" id="message" name="message" placeholder="Enter message to send" required>
    <input type="submit" id="sendButton" value="Send Message">
  </form>
  <div id="status"></div>
  <hr>
  <h2>Update Display Title</h2>
  <form id="titleForm">
    <input type="text" id="newTitle" name="title" placeholder="New title (max 10 chars)" maxlength="10" required>
    <input type="submit" id="titleButton" value="Update Title">
  </form>
  <div id="titleStatus"></div>
  <hr>
  <h2>Update Sensor Interval</h2>
  <form id="intervalForm">
    <input type="number" id="newInterval" name="interval" placeholder="New interval in seconds (min 90)" min="90" required>
    <input type="submit" id="intervalButton" value="Update Interval">
  </form>
  <div id="intervalStatus"></div>
  <hr>
  <h2>Update Gas Sensor Ro</h2>
  <form id="roForm">
    <input type="number" id="newRo" name="ro" placeholder="New Ro value (e.g., 30000)" step="any" required>
    <input type="submit" id="roButton" value="Update Ro">
  </form>
  <div id="roStatus"></div>
  <hr>
  <h2>Update Water Temperature</h2>
  <form id="waterTempForm">
    <input type="number" id="newWaterTemp" name="waterTemp" placeholder="New water temperature (°C)" required>
    <input type="submit" id="waterTempButton" value="Update Water Temperature">
  </form>
  <div id="waterTempStatus"></div>
</div>
<script>
  const sendForm = document.getElementById('sendForm');
  const sendButton = document.getElementById('sendButton');
  const statusDiv = document.getElementById('status');
  let pollingInterval;

  sendForm.addEventListener('submit', function(e) {
    e.preventDefault();
    const message = document.getElementById('message').value;
    
    sendButton.disabled = true;
    statusDiv.className = '';
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
      statusDiv.className = 'error';
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
          statusDiv.className = 'success';
          statusDiv.textContent = 'Message Sent Successfully!';
          sendButton.disabled = false;
        } else if (statusText === 'FAILED') {
          clearInterval(pollingInterval);
          statusDiv.className = 'error';
          statusDiv.textContent = 'Failed: Message could not be sent.';
          sendButton.disabled = false;
        } else if (statusText === 'SENDING') {
          statusDiv.textContent = 'Waiting for confirmation...';
        }
      })
      .catch(error => {
        clearInterval(pollingInterval);
        statusDiv.className = 'error';
        statusDiv.textContent = 'Error: Lost connection to server.';
        sendButton.disabled = false;
      });
  }

  const titleForm = document.getElementById('titleForm');
  const titleButton = document.getElementById('titleButton');
  const titleStatusDiv = document.getElementById('titleStatus');

  titleForm.addEventListener('submit', function(e) {
    e.preventDefault();
    const newTitle = document.getElementById('newTitle').value;

    titleButton.disabled = true;
    titleStatusDiv.textContent = 'Updating...';

    fetch('/settitle', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'title=' + encodeURIComponent(newTitle)
    })
    .then(response => response.text().then(text => ({ ok: response.ok, text })))
    .then(({ ok, text }) => {
      if (ok) {
        titleStatusDiv.className = 'success';
        titleStatusDiv.textContent = text;
      } else {
        titleStatusDiv.className = 'error';
        titleStatusDiv.textContent = 'Error: ' + text;
      }
      titleButton.disabled = false;
    })
    .catch(error => {
      titleStatusDiv.className = 'error';
      titleStatusDiv.textContent = 'Error: Could not update title.';
      titleButton.disabled = false;
    });
  });

  const intervalForm = document.getElementById('intervalForm');
  const intervalButton = document.getElementById('intervalButton');
  const intervalStatusDiv = document.getElementById('intervalStatus');

  intervalForm.addEventListener('submit', function(e) {
    e.preventDefault();
    const newInterval = document.getElementById('newInterval').value;

    intervalButton.disabled = true;
    intervalStatusDiv.textContent = 'Updating...';

    fetch('/setinterval', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'interval=' + encodeURIComponent(newInterval)
    })
    .then(response => response.text().then(text => ({ ok: response.ok, text })))
    .then(({ ok, text }) => {
      if (ok) {
        intervalStatusDiv.className = 'success';
        intervalStatusDiv.textContent = text;
      } else {
        intervalStatusDiv.className = 'error';
        intervalStatusDiv.textContent = 'Error: ' + text;
      }
      intervalButton.disabled = false;
    })
    .catch(error => {
      intervalStatusDiv.className = 'error';
      intervalStatusDiv.textContent = 'Error: Could not update interval.';
      intervalButton.disabled = false;
    });
  });

  const roForm = document.getElementById('roForm');
  const roButton = document.getElementById('roButton');
  const roStatusDiv = document.getElementById('roStatus');

  roForm.addEventListener('submit', function(e) {
    e.preventDefault();
    const newRo = document.getElementById('newRo').value;

    roButton.disabled = true;
    roStatusDiv.textContent = 'Updating...';

    fetch('/setro', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'ro=' + encodeURIComponent(newRo)
    })
    .then(response => response.text().then(text => ({ ok: response.ok, text })))
    .then(({ ok, text }) => {
      if (ok) {
        roStatusDiv.className = 'success';
        roStatusDiv.textContent = text;
      } else {
        roStatusDiv.className = 'error';
        roStatusDiv.textContent = 'Error: ' + text;
      }
      roButton.disabled = false;
    })
    .catch(error => {
      roStatusDiv.className = 'error';
      roStatusDiv.textContent = 'Error: Could not update Ro.';
      roButton.disabled = false;
    });
  });
</script>
</body></html>)rawliteral";
