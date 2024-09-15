<?php
// Define SQLite database file name
$db_file = 'stations.db';

// Create or open SQLite database
$db = new PDO('sqlite:' . $db_file);
$db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

// Create the table if it does not exist yet
$db->exec("CREATE TABLE IF NOT EXISTS stations (
    serial TEXT PRIMARY KEY,
    stations TEXT
)");

// Retrieve the serial number and action from GET parameters
$serial = isset($_GET['serial']) ? $_GET['serial'] : null;
$action = isset($_GET['action']) ? $_GET['action'] : null;

// If the serial number is not provided, display an error
if (!$serial) {
    echo json_encode(["error" => "Serial number not provided."]);
    exit;
}

// If the action is 'get', return the list of stations in JSON
if ($action === 'get') {
    $stmt = $db->prepare("SELECT stations FROM stations WHERE serial = :serial");
    $stmt->bindParam(':serial', $serial);
    $stmt->execute();
    $result = $stmt->fetch(PDO::FETCH_ASSOC);

    if ($result) {
        // Return the list of stations as a JSON array
        echo $result['stations'];
    } else {
        // If no stations are found, return an empty array
        echo "[]";
    }
    exit;
}

// If the form is submitted, save the stations in the database
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    // Retrieve the stations from the form
    $stations = isset($_POST['stations']) ? json_decode($_POST['stations'], true) : [];

    // Ensure $stations is an array
    if (!is_array($stations)) {
        $stations = [];
    }

    // Convert the array of stations to JSON
    $stations_json = json_encode($stations);

    // Insert or update the stations in the database
    $stmt = $db->prepare("REPLACE INTO stations (serial, stations) VALUES (:serial, :stations)");
    $stmt->bindParam(':serial', $serial);
    $stmt->bindParam(':stations', $stations_json);
    $stmt->execute();

    echo json_encode(["message" => "Stations successfully updated. Please turn off and on your Radio to update your stations."]);
    exit;
}

// Retrieve the current list of stations to pre-fill the form
$stmt = $db->prepare("SELECT stations FROM stations WHERE serial = :serial");
$stmt->bindParam(':serial', $serial);
$stmt->execute();
$result = $stmt->fetch(PDO::FETCH_ASSOC);

$current_stations = $result ? json_decode($result['stations'], true) : [];

// List of available stations
$available_stations = [
    ['name' => 'France Inter', 'url' => 'http://direct.franceinter.fr/live/franceinter-midfi.mp3'],
    ['name' => 'France Culture', 'url' => 'http://icecast.radiofrance.fr/franceculture-midfi.mp3'],
    ['name' => 'RFI Monde', 'url' => 'http://live02.rfi.fr/rfimonde-64.mp3'],
    ['name' => 'BBC World Service', 'url' => 'http://stream.live.vc.bbcmedia.co.uk/bbc_world_service'],
    ['name' => 'NPR News', 'url' => 'https://npr-ice.streamguys1.com/live.mp3'],
    ['name' => 'Classic FM', 'url' => 'http://media-ice.musicradio.com/ClassicFMMP3'],
    ['name' => 'Smooth Jazz', 'url' => 'http://smoothjazz.com/streams/smoothjazz_128.pls'],
    ['name' => 'France Inter', 'url' => 'http://direct.franceinter.fr/live/franceinter-midfi.mp3'],
    ['name' => 'France Culture', 'url' => 'http://icecast.radiofrance.fr/franceculture-midfi.mp3'],
    ['name' => 'RFI Monde', 'url' => 'http://live02.rfi.fr/rfimonde-64.mp3'],
    ['name' => 'BBC World Service', 'url' => 'http://stream.live.vc.bbcmedia.co.uk/bbc_world_service'],
    ['name' => 'NPR News', 'url' => 'https://npr-ice.streamguys1.com/live.mp3'],
    ['name' => 'Classic FM', 'url' => 'http://media-ice.musicradio.com/ClassicFMMP3'],
    ['name' => 'Smooth Jazz', 'url' => 'http://smoothjazz.com/streams/smoothjazz_128.pls'],
    ['name' => 'Radio Paradise', 'url' => 'http://stream.radioparadise.com/mp3-128'],
    ['name' => 'FIP', 'url' => 'http://icecast.radiofrance.fr/fip-midfi.mp3'],
    ['name' => 'France Musique', 'url' => 'http://icecast.radiofrance.fr/francemusique-midfi.mp3'],
    ['name' => 'France Info', 'url' => 'http://icecast.radiofrance.fr/franceinfo-midfi.mp3'],
    ['name' => 'RTL', 'url' => 'http://streaming.radio.rtl.fr/rtl-1-44-128'],
    ['name' => 'Europe 1', 'url' => 'http://ais-live.cloud-services.paris:8000/europe1.mp3'],
    ['name' => 'Radio Nova', 'url' => 'http://novazz.ice.infomaniak.ch/novazz-128.mp3'],
    ['name' => 'TSF Jazz', 'url' => 'http://tsfjazz.ice.infomaniak.ch/tsfjazz-high.mp3'],
    ['name' => 'Jazz24', 'url' => 'https://live.wostreaming.net/direct/ppm-jazz24aac-ibc1'],
    ['name' => 'BBC Radio 1', 'url' => 'http://stream.live.vc.bbcmedia.co.uk/bbc_radio_one'],
    ['name' => 'BBC Radio 2', 'url' => 'http://stream.live.vc.bbcmedia.co.uk/bbc_radio_two'],
    ['name' => 'BBC Radio 3', 'url' => 'http://stream.live.vc.bbcmedia.co.uk/bbc_radio_three'],
    ['name' => 'BBC Radio 4', 'url' => 'http://stream.live.vc.bbcmedia.co.uk/bbc_radio_fourfm'],
    ['name' => 'WNYC', 'url' => 'http://fm939.wnyc.org/wnycfm'],
    ['name' => 'KEXP', 'url' => 'http://live-mp3-128.kexp.org/kexp128.mp3'],
    ['name' => 'WBGO Jazz 88.3', 'url' => 'http://wbgo.streamguys.net/wbgo'],
    ['name' => 'Venice Classic Radio', 'url' => 'http://174.36.206.197:8000/stream'],
    ['name' => 'Radio Swiss Classic', 'url' => 'http://stream.srg-ssr.ch/m/rsc_de/mp3_128'],
    ['name' => 'Radio Swiss Jazz', 'url' => 'http://stream.srg-ssr.ch/m/rsj/mp3_128'],
    ['name' => 'Soma FM Groove Salad', 'url' => 'http://ice1.somafm.com/groovesalad-128-mp3'],
    ['name' => 'Soma FM Drone Zone', 'url' => 'http://ice1.somafm.com/dronezone-128-mp3'],
    ['name' => 'Digitally Imported Lounge', 'url' => 'http://prem1.di.fm:80/lounge'],
    ['name' => 'KCRW', 'url' => 'http://kcrw.streamguys1.com/kcrw_192k_mp3_on_air'],
    ['name' => 'FIP Rock', 'url' => 'http://icecast.radiofrance.fr/fiprock-midfi.mp3'],
    ['name' => 'FIP Jazz', 'url' => 'http://icecast.radiofrance.fr/fipjazz-midfi.mp3'],
    ['name' => 'FIP Groove', 'url' => 'http://icecast.radiofrance.fr/fipgroove-midfi.mp3'],
    ['name' => 'FIP World', 'url' => 'http://icecast.radiofrance.fr/fipworld-midfi.mp3'],
    ['name' => 'Radio Classique', 'url' => 'http://radioclassique.ice.infomaniak.ch/radioclassique-high.mp3'],
    ['name' => 'Nostalgie', 'url' => 'http://cdn.nrjaudio.fm/audio1/fr/30601/mp3_128.mp3'],
    ['name' => 'RMC', 'url' => 'http://rmc.bfmtv.com/rmcinfo-mp3'],
    ['name' => 'Radio Meuh', 'url' => 'http://radiomeuh.ice.infomaniak.ch/radiomeuh-128.mp3'],
    ['name' => 'NTS Radio', 'url' => 'https://stream-relay-geo.ntslive.net/stream'],
    ['name' => 'Worldwide FM', 'url' => 'http://worldwidefm.out.airtime.pro:8000/worldwidefm_a'],
    ['name' => 'Rinse FM', 'url' => 'http://206.189.117.157:8000/stream'],
    ['name' => 'Radio Paradise', 'url' => 'http://stream.radioparadise.com/mp3-128']
];

// Presets
$presets = [
    'News' => ['France Inter', 'RFI Monde', 'BBC World Service', 'NPR News'],
    'Culture' => ['France Culture', 'Classic FM', 'Radio Paradise'],
    'Music' => ['Smooth Jazz', 'Radio Paradise', 'Classic FM']
];
?>

<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Customize Your Stations</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background-color: #87BCBF;
            color: #333;
            line-height: 1.6;
            padding: 20px;
        }
        .container {
            max-width: 800px;
            margin: 0 auto;
            background-color: #F5D8A3;
            padding: 20px;
            border-radius: 5px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        h1 {
            color: #87BCBF;
            text-align: center;
        }
        .station-row {
            margin-bottom: 15px;
            padding: 10px;
            background-color: #fff;
            border-radius: 3px;
        }
        label {
            display: block;
            margin-bottom: 5px;
        }
        select, input[type="text"] {
            width: 100%;
            padding: 8px;
            margin-bottom: 10px;
            border: 1px solid #ddd;
            border-radius: 3px;
        }
        button {
            background-color: #87BCBF;
            color: white;
            border: none;
            padding: 10px 15px;
            cursor: pointer;
            border-radius: 3px;
        }
        button:hover {
            background-color: #6BA3A7;
        }
        .add-btn, .submit-btn {
            display: block;
            width: 100%;
            margin-top: 10px;
        }
        .preset-buttons {
            margin-bottom: 20px;
            text-align: center;
        }
        .preset-btn {
            margin: 5px;
            background-color: #F5D8A3;
            color: #333;
            border: 1px solid #87BCBF;
        }
        .preset-btn:hover {
            background-color: #EFC88D;
        }
        footer {
            text-align: center;
            margin-top: 20px;
            color: #FFF;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Customize Your Stations</h1>
        <div class="preset-buttons">
            <?php foreach ($presets as $name => $stations): ?>
                <button type="button" class="preset-btn" onclick="applyPreset('<?= $name ?>')"><?= $name ?></button>
            <?php endforeach; ?>
        </div>
        <form id="stationForm" method="post">
            <div id="station-fields">
                <!-- Existing stations will be inserted here -->
            </div>
            <button type="button" class="add-btn" onclick="addStation()">Add Another Station</button>
            <input type="hidden" name="stations" id="stationsJson">
            <input type="submit" class="submit-btn" value="Save">
        </form>
    </div>
    <footer>
        &copy; 2024 RaspiAudio - Muse Radio
    </footer>

    <script>
        let stationCount = 0;
        const maxStations = 10;
        const availableStations = <?= json_encode($available_stations) ?>;
        const presets = <?= json_encode($presets) ?>;

        function addStation(name = '', url = '') {
            if (stationCount >= maxStations) return;

            const container = document.getElementById('station-fields');
            const stationRow = document.createElement('div');
            stationRow.className = 'station-row';
            stationRow.innerHTML = `
                <label for="stationName${stationCount}">Station ${stationCount + 1} - Name:</label>
                <input type="text" id="stationName${stationCount}" value="${name}" placeholder="Enter station name">
                <label for="stationURL${stationCount}">URL:</label>
                <input type="text" id="stationURL${stationCount}" value="${url}" placeholder="Enter the station URL">
                <select onchange="updateStation(${stationCount})">
                    <option value="">-- Select a station --</option>
                    ${availableStations.map(station => `
                        <option value="${station.name}">${station.name}</option>
                    `).join('')}
                </select>
                <button type="button" onclick="removeStation(${stationCount})">Remove</button>
            `;
            container.appendChild(stationRow);

            stationCount++;
        }

        function updateStation(index) {
            const select = event.target;
            const selectedStation = availableStations.find(station => station.name === select.value);
            if (selectedStation) {
                document.getElementById(`stationName${index}`).value = selectedStation.name;
                document.getElementById(`stationURL${index}`).value = selectedStation.url;
            }
        }

        function removeStation(index) {
            const container = document.getElementById('station-fields');
            const stationRow = document.getElementById(`stationName${index}`).closest('.station-row');
            container.removeChild(stationRow);
            stationCount--;
        }

        function applyPreset(presetName) {
            const container = document.getElementById('station-fields');
            container.innerHTML = '';
            stationCount = 0;

            presets[presetName].forEach(stationName => {
                const station = availableStations.find(s => s.name === stationName);
                if (station) {
                    addStation(station.name, station.url);
                }
            });
        }

        // Initialize form with existing stations
        <?php foreach ($current_stations as $station): ?>
            addStation("<?= htmlspecialchars($station['name']) ?>", "<?= htmlspecialchars($station['url']) ?>");
        <?php endforeach; ?>

        if (stationCount === 0) {
            addStation(); // Add one empty station if none exists
        }

        // Add event listener to the form submission
        document.getElementById('stationForm').addEventListener('submit', function(e) {
            e.preventDefault();
            const stations = [];
            for (let i = 0; i < stationCount; i++) {
                const name = document.getElementById(`stationName${i}`).value;
                const url = document.getElementById(`stationURL${i}`).value;
                if (name && url) {
                    stations.push({ name, url });
                }
            }
            document.getElementById('stationsJson').value = JSON.stringify(stations);
            this.submit();
        });
    </script>
</body>
</html>
