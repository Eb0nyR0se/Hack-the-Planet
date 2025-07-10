document.addEventListener("DOMContentLoaded", () => {
  const datasetSelect = document.getElementById("dataset");
  const recordsDiv = document.getElementById("records");

  datasetSelect.addEventListener("change", () => {
    loadDataset(datasetSelect.value);
  });

  loadDataset(datasetSelect.value);

  function loadDataset(file) {
    fetch(file)
      .then(res => res.json())
      .then(data => {
        recordsDiv.innerHTML = "";
        data.records.filter(r => !r.verified).forEach((record, i) => {
          const div = document.createElement("div");
          div.className = "record";

          const content = `
            <strong>ID:</strong> ${record.id}<br/>
            <strong>Suggested Species:</strong> ${record.suggested_species || "Unknown"}<br/>
            <strong>Location:</strong> ${record.location?.lat}, ${record.location?.lon}<br/>
            <strong>Signal:</strong> Freq: ${record.signal?.freq_peak} Hz, Amp: ${record.signal?.amplitude}<br/>
            ${record.image ? `<img src="${record.image}" alt="plant image"/>` : ""}
            ${record.audio ? `<audio controls src="${record.audio}"></audio>` : ""}
            <br/>
            <button class="approve">Approve</button>
            <button class="reject">Reject</button>
          `;

          div.innerHTML = content;

          div.querySelector(".approve").onclick = () => handleDecision(record, true, i);
          div.querySelector(".reject").onclick = () => handleDecision(record, false, i);

          recordsDiv.appendChild(div);
        });
      });
  }

  function handleDecision(record, approved, index) {
    alert(`Record ${record.id} ${approved ? "approved" : "rejected"}.`);
    // In a real setup, you'd PATCH this change via GitHub API or Replit backend
    record.verified = approved;
    record.verified_species = approved ? record.suggested_species : "rejected";
    // For now, just reload
    loadDataset(datasetSelect.value);
  }
});
