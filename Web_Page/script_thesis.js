let uploadedFiles = [];
let parsedData = [];
let currentCharts = [];
let isLogScale = false;
let currentAnalysisType = null;

async function saveToStorage() {
  try {
    const data = {
      files: uploadedFiles.map((f) => f.name),
      parsedData: parsedData,
      timestamp: Date.now(),
    };
    await window.storage.set("benchmark-data", JSON.stringify(data));
  } catch (error) {
    console.log("Storage not available:", error);
  }
}

async function loadFromStorage() {
  try {
    const result = await window.storage.get("benchmark-data");
    if (result && result.value) {
      const data = JSON.parse(result.value);
      if (Date.now() - data.timestamp < 24 * 60 * 60 * 1000) {
        parsedData = data.parsedData;
        if (parsedData.length > 0) {
          const algorithms = [...new Set(parsedData.map((d) => d.algorithm))];
          if (algorithms.length === 1) {
            performStabilityAnalysis();
          } else {
            performComparisonAnalysis();
          }
          document.getElementById("analysis-section").style.display = "block";
        }
      }
    }
  } catch (error) {
    console.log("Could not load from storage:", error);
  }
}

window.addEventListener("load", loadFromStorage);

document.getElementById("file-upload").addEventListener("change", function (e) {
  const files = Array.from(e.target.files);
  files.forEach((file) => {
    if (!uploadedFiles.some((f) => f.name === file.name)) {
      uploadedFiles.push(file);
    }
  });
  updateFileList();
  updateAnalyzeButton();
});

function updateFileList() {
  const fileList = document.getElementById("file-list");
  fileList.innerHTML = "";

  uploadedFiles.forEach((file, index) => {
    const fileItem = document.createElement("div");
    fileItem.className = "file-item";
    fileItem.innerHTML = `
            <span>${file.name}</span>
            <button class="remove-file" onclick="removeFile(${index})">Remove</button>
        `;
    fileList.appendChild(fileItem);
  });
}

function removeFile(index) {
  uploadedFiles.splice(index, 1);
  updateFileList();
  updateAnalyzeButton();
  hideResults();
}

function clearAllFiles() {
  if (confirm("Are you sure you want to remove all files?")) {
    uploadedFiles = [];
    updateFileList();
    updateAnalyzeButton();
    hideResults();
  }
}

function hideResults() {
  document.getElementById("analysis-section").style.display = "none";
  document.getElementById("export-controls").style.display = "none";
  parsedData = [];
  currentAnalysisType = null;
}

function updateAnalyzeButton() {
  const analyzeBtn = document.getElementById("analyze-btn");
  const clearBtn = document.getElementById("clear-all-btn");
  const hasFiles = uploadedFiles.length > 0;

  analyzeBtn.disabled = !hasFiles;
  clearBtn.disabled = !hasFiles;
}

async function analyzeFiles() {
  if (uploadedFiles.length === 0) return;

  if (uploadedFiles.length === 1) {
    alert(
      "Please upload more files. You need at least 2 files to perform analysis."
    );
    return;
  }

  document.getElementById("loading").style.display = "block";
  document.getElementById("analysis-section").style.display = "none";

  try {
    parsedData = [];
    for (const file of uploadedFiles) {
      const content = await readFile(file);
      const parsed = parseLogFile(content, file.name);
      if (parsed) parsedData.push(parsed);
    }

    if (parsedData.length === 0) {
      alert("No valid benchmark files found");
      document.getElementById("loading").style.display = "none";
      return;
    }

    const algorithms = [...new Set(parsedData.map((d) => d.algorithm))];

    if (algorithms.length === 1) {
      currentAnalysisType = "stability";
      performStabilityAnalysis();
    } else {
      currentAnalysisType = "comparison";
      performComparisonAnalysis();
    }

    await saveToStorage();

    document.getElementById("loading").style.display = "none";
    document.getElementById("analysis-section").style.display = "block";
    document.getElementById("export-controls").style.display = "flex";
  } catch (error) {
    console.error("Analysis error:", error);
    alert("Error analyzing files: " + error.message);
    document.getElementById("loading").style.display = "none";
  }
}

function readFile(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = (e) => resolve(e.target.result);
    reader.onerror = reject;
    reader.readAsText(file);
  });
}

function parseLogFile(content, filename) {
  try {
    const data = {
      filename: filename,
      algorithm: "",
      
      signing: {
        throughput: NaN,
        
        energyPkg: NaN,
        energyCores: NaN,
        
        dynamicPkg: NaN,
        dynamicCores: NaN,
      },
      verification: {
        throughput: NaN,
        energyPkg: NaN,
        energyCores: NaN,
        dynamicPkg: NaN,
        dynamicCores: NaN,
      },
      idle: {
        avgPkgPowerW: NaN,
        avgCoresPowerW: NaN,
      },
    };

    
    const algMatch = content.match(/Algorithm:\s+(.+)/);
    if (algMatch) data.algorithm = algMatch[1].trim();

    
    const signThroughputMatch = content.match(
      /Signing:\s+\d+\s+ops\s+in\s+[\d.]+s\s+->\s+([\d.]+)\s+ops\/sec/
    );
    if (signThroughputMatch) data.signing.throughput = parseFloat(signThroughputMatch[1]);

    const verifyThroughputMatch = content.match(
      /Verification:\s+\d+\s+ops\s+in\s+[\d.]+s\s+->\s+([\d.]+)\s+ops\/sec/
    );
    if (verifyThroughputMatch) data.verification.throughput = parseFloat(verifyThroughputMatch[1]);

    
    const signPkgEnergyMatch = content.match(
      /Signing\s+\(CPU\):\s+[\d.]+\s*J\s+total,\s*([\d.]+)\s*mJ\/op/
    );
    if (signPkgEnergyMatch) data.signing.energyPkg = parseFloat(signPkgEnergyMatch[1]);

    const verifyPkgEnergyMatch = content.match(
      /Verification\s+\(CPU\):\s+[\d.]+\s*J\s+total,\s*([\d.]+)\s*mJ\/op/
    );
    if (verifyPkgEnergyMatch) data.verification.energyPkg = parseFloat(verifyPkgEnergyMatch[1]);

    const signCoresEnergyMatch = content.match(
      /Signing\s+\(Cores\):\s+[\d.]+\s*J\s+total,\s*([\d.]+)\s*mJ\/op/
    );
    if (signCoresEnergyMatch) data.signing.energyCores = parseFloat(signCoresEnergyMatch[1]);

    const verifyCoresEnergyMatch = content.match(
      /Verification\s+\(Cores\):\s+[\d.]+\s*J\s+total,\s*([\d.]+)\s*mJ\/op/
    );
    if (verifyCoresEnergyMatch) data.verification.energyCores = parseFloat(verifyCoresEnergyMatch[1]);

    
    const idleMatch = content.match(
      /Idle\s+baseline\s+completed\.\s+Avg\s+CPU\s+Power:\s+([\d.]+)\s*W,\s+Avg\s+Cores\s+Power:\s+([\d.]+)\s*W/
    );
    if (idleMatch) {
      data.idle.avgPkgPowerW = parseFloat(idleMatch[1]);
      data.idle.avgCoresPowerW = parseFloat(idleMatch[2]);
    }

    
    const pkgDynSection = content.match(
      /Dynamic\s+Energy\s+Analysis\s+\(CPU\s+Package,\s+Idle-Subtracted\):[\s\S]*?Signing:\s+([\d.]+)\s*mJ\/op[\s\S]*?Verification:\s+([\d.]+)\s*mJ\/op/
    );
    if (pkgDynSection) {
      data.signing.dynamicPkg = parseFloat(pkgDynSection[1]);
      data.verification.dynamicPkg = parseFloat(pkgDynSection[2]);
    }

    const coresDynSection = content.match(
      /Dynamic\s+Energy\s+Analysis\s+\(CPU\s+Cores,\s+Idle-Subtracted\):[\s\S]*?Signing:\s+([\d.]+)\s*mJ\/op[\s\S]*?Verification:\s+([\d.]+)\s*mJ\/op/
    );
    if (coresDynSection) {
      data.signing.dynamicCores = parseFloat(coresDynSection[1]);
      data.verification.dynamicCores = parseFloat(coresDynSection[2]);
    }

    
    
    
    if (!data.algorithm) return null;

    const hasAnyMetric =
      Number.isFinite(data.signing.throughput) ||
      Number.isFinite(data.verification.throughput) ||
      Number.isFinite(data.signing.dynamicPkg) ||
      Number.isFinite(data.verification.dynamicPkg) ||
      Number.isFinite(data.signing.dynamicCores) ||
      Number.isFinite(data.verification.dynamicCores);

    if (!hasAnyMetric) return null;

    return data;

  } catch (error) {
    console.error("Error parsing file:", filename, error);
    return null;
  }
}

function toggleScale() {
  isLogScale = !isLogScale;
  const btn = document.getElementById("scale-toggle");
  btn.textContent = isLogScale
    ? "📊 Switch to Linear Scale"
    : "📈 Switch to Log Scale";
  btn.classList.toggle("active", isLogScale);

  if (currentAnalysisType === "stability") {
    performStabilityAnalysis();
  } else if (currentAnalysisType === "comparison") {
    performComparisonAnalysis();
  }
}

function downloadCanvasPNG(canvasId, baseName, scale = 4) {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;

  
  const w = canvas.width;
  const h = canvas.height;

  const exportCanvas = document.createElement("canvas");
  exportCanvas.width = w * scale;
  exportCanvas.height = h * scale;

  const ctx = exportCanvas.getContext("2d");
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = "high";

  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, exportCanvas.width, exportCanvas.height);

  ctx.drawImage(canvas, 0, 0, exportCanvas.width, exportCanvas.height);

  exportCanvas.toBlob((blob) => {
    const url = window.URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `${baseName}_${Date.now()}.png`;
    a.click();
    window.URL.revokeObjectURL(url);
  }, "image/png");
}

function performStabilityAnalysis() {
  const algorithm = parsedData[0].algorithm;
  document.getElementById(
    "analysis-type"
  ).textContent = `📊 Stability Analysis for ${algorithm} (${parsedData.length} runs)`;

  const container = document.getElementById("results-container");
  container.innerHTML = "";

  const stats = calculateStabilityStats();

  const statsGrid = document.createElement("div");
  statsGrid.className = "stats-grid";
  statsGrid.innerHTML = `
        <div class="stat-card">
            <div class="stat-value">${fmtNumber(stats.signing.throughput.mean,2)}</div>
            <div class="stat-label">Avg Signing Throughput (ops/sec)</div>
            <div style="font-size: 12px; margin-top: 5px;">
                CV: ${Number.isFinite(stats.signing.throughput.cv)?(stats.signing.throughput.cv*100).toFixed(2)+'%':'—'}
                ${getStabilityIndicator(stats.signing.throughput.cv)}
            </div>
        </div>
        <div class="stat-card">
            <div class="stat-value">${fmtNumber(stats.verification.throughput.mean,2)}</div>
            <div class="stat-label">Avg Verification Throughput (ops/sec)</div>
            <div style="font-size: 12px; margin-top: 5px;">
                CV: ${Number.isFinite(stats.verification.throughput.cv)?(stats.verification.throughput.cv*100).toFixed(2)+'%':'—'}
                ${getStabilityIndicator(stats.verification.throughput.cv)}
            </div>
        </div>

        <div class="stat-card">
            <div class="stat-value">${fmtNumber(stats.signing.dynamicPkg.mean,3)}</div>
            <div class="stat-label">Avg Signing Energy (CPU Package, mJ/op)</div>
            <div style="font-size: 12px; margin-top: 5px;">
                CV: ${Number.isFinite(stats.signing.dynamicPkg.cv)?(stats.signing.dynamicPkg.cv*100).toFixed(2)+'%':'—'}
                ${getStabilityIndicator(stats.signing.dynamicPkg.cv)}
            </div>
        </div>
        <div class="stat-card">
            <div class="stat-value">${fmtNumber(stats.verification.dynamicPkg.mean,3)}</div>
            <div class="stat-label">Avg Verification Energy (CPU Package, mJ/op)</div>
            <div style="font-size: 12px; margin-top: 5px;">
                CV: ${Number.isFinite(stats.verification.dynamicPkg.cv)?(stats.verification.dynamicPkg.cv*100).toFixed(2)+'%':'—'}
                ${getStabilityIndicator(stats.verification.dynamicPkg.cv)}
            </div>
        </div>

        <div class="stat-card">
            <div class="stat-value">${fmtNumber(stats.signing.dynamicCores.mean,3)}</div>
            <div class="stat-label">Avg Signing Energy (CPU Cores, mJ/op)</div>
            <div style="font-size: 12px; margin-top: 5px;">
                CV: ${Number.isFinite(stats.signing.dynamicCores.cv)?(stats.signing.dynamicCores.cv*100).toFixed(2)+'%':'—'}
                ${getStabilityIndicator(stats.signing.dynamicCores.cv)}
            </div>
        </div>
        <div class="stat-card">
            <div class="stat-value">${fmtNumber(stats.verification.dynamicCores.mean,3)}</div>
            <div class="stat-label">Avg Verification Energy (CPU Cores, mJ/op)</div>
            <div style="font-size: 12px; margin-top: 5px;">
                CV: ${Number.isFinite(stats.verification.dynamicCores.cv)?(stats.verification.dynamicCores.cv*100).toFixed(2)+'%':'—'}
                ${getStabilityIndicator(stats.verification.dynamicCores.cv)}
            </div>
        </div>
    `;
  container.appendChild(statsGrid);

  createStabilityCharts(container);
  createStabilityTable(container);
}

function performComparisonAnalysis() {
  const algorithms = [...new Set(parsedData.map((d) => d.algorithm))];
  document.getElementById(
    "analysis-type"
  ).textContent = `⚖️ Algorithm Comparison (${algorithms.length} algorithms)`;

  const container = document.getElementById("results-container");
  container.innerHTML = "";

  const groupedData = {};
  parsedData.forEach((data) => {
    if (!groupedData[data.algorithm]) {
      groupedData[data.algorithm] = [];
    }
    groupedData[data.algorithm].push(data);
  });

  const comparisonData = Object.keys(groupedData).map((alg) => {
    const runs = groupedData[alg];

    const signingThroughputStats = calculateMetricStats(
      runs.map((r) => r.signing.throughput)
    );
    const verifyThroughputStats = calculateMetricStats(
      runs.map((r) => r.verification.throughput)
    );

    const signingPkgStats = calculateMetricStats(runs.map((r) => r.signing.dynamicPkg));
    const verifyPkgStats = calculateMetricStats(runs.map((r) => r.verification.dynamicPkg));

    const signingCoresStats = calculateMetricStats(runs.map((r) => r.signing.dynamicCores));
    const verifyCoresStats = calculateMetricStats(runs.map((r) => r.verification.dynamicCores));

    return {
      algorithm: alg,
      runs: runs.length,
      signing: {
        throughput: signingThroughputStats.mean,
        energyPkg: signingPkgStats.mean,
        energyCores: signingCoresStats.mean,
        stats: {
          throughput: signingThroughputStats,
          energyPkg: signingPkgStats,
          energyCores: signingCoresStats,
        },
      },
      verification: {
        throughput: verifyThroughputStats.mean,
        energyPkg: verifyPkgStats.mean,
        energyCores: verifyCoresStats.mean,
        stats: {
          throughput: verifyThroughputStats,
          energyPkg: verifyPkgStats,
          energyCores: verifyCoresStats,
        },
      },
    };
  });

  createRecommendations(container, comparisonData);

  const statsGrid = document.createElement("div");
  statsGrid.className = "stats-grid";

  const bestSigning = comparisonData.reduce((best, curr) =>
    curr.signing.throughput > best.signing.throughput ? curr : best
  );
  const bestVerification = comparisonData.reduce((best, curr) =>
    curr.verification.throughput > best.verification.throughput ? curr : best
  );

  
  const mostEfficientPkg = comparisonData.reduce((best, curr) =>
    curr.signing.energyPkg + curr.verification.energyPkg <
    best.signing.energyPkg + best.verification.energyPkg
      ? curr
      : best
  );

  statsGrid.innerHTML = `
        <div class="stat-card">
            <div class="stat-value">${bestSigning.signing.throughput.toFixed(0)}</div>
            <div class="stat-label">Best Signing Performance</div>
            <div style="font-size: 12px; margin-top: 5px;">${bestSigning.algorithm}</div>
        </div>
        <div class="stat-card">
            <div class="stat-value">${bestVerification.verification.throughput.toFixed(0)}</div>
            <div class="stat-label">Best Verification Performance</div>
            <div style="font-size: 12px; margin-top: 5px;">${bestVerification.algorithm}</div>
        </div>
        <div class="stat-card">
            <div class="stat-value">${(
              mostEfficientPkg.signing.energyPkg + mostEfficientPkg.verification.energyPkg
            ).toFixed(3)}</div>
            <div class="stat-label">Most Energy Efficient (CPU Package, mJ/op)</div>
            <div style="font-size: 12px; margin-top: 5px;">${mostEfficientPkg.algorithm}</div>
        </div>
        <div class="stat-card">
            <div class="stat-value">${algorithms.length}</div>
            <div class="stat-label">Algorithms Compared</div>
            <div style="font-size: 12px; margin-top: 5px;">${parsedData.length} total runs</div>
        </div>
    `;
  container.appendChild(statsGrid);

  createComparisonCharts(container, comparisonData);
  createComparisonTable(container, comparisonData);
}

function createRecommendations(container, comparisonData) {
  const recSection = document.createElement("div");
  recSection.className = "recommendations";

  let html = "<h3>🎯 Algorithm Recommendations</h3>";

  function safeBest(arr, valueFn, lowerIsBetter = false) {
    const valid = arr.filter((d) => Number.isFinite(valueFn(d)));
    if (valid.length === 0) return null;
    return valid.reduce((best, curr) => {
      const bv = valueFn(best);
      const cv = valueFn(curr);
      return lowerIsBetter ? (cv < bv ? curr : best) : (cv > bv ? curr : best);
    });
  }

  function isNearTie(a, b, valueFn) {
    if (!a || !b) return false;
    const av = valueFn(a);
    const bv = valueFn(b);
    if (!Number.isFinite(av) || !Number.isFinite(bv) || av === 0) return false;
    return Math.abs(av - bv) / Math.max(av, bv) < 0.05;
  }

  const bestSigningPerf   = safeBest(comparisonData, (d) => d.signing.throughput);
  const bestVerifyPerf    = safeBest(comparisonData, (d) => d.verification.throughput);
  const bestSigningEnergy = safeBest(comparisonData, (d) => d.signing.energyPkg, true);
  const bestVerifyEnergy  = safeBest(comparisonData, (d) => d.verification.energyPkg, true);
  const bestOverallEnergy = safeBest(
    comparisonData,
    (d) => d.signing.energyPkg + d.verification.energyPkg,
    true
  );

  const withScores = comparisonData.map((d) => {
    const totalThroughput = d.signing.throughput + d.verification.throughput;
    const totalEnergy = d.signing.energyPkg + d.verification.energyPkg;
    const balanceScore =
      Number.isFinite(totalThroughput) && Number.isFinite(totalEnergy) && totalEnergy > 0
        ? totalThroughput / totalEnergy
        : NaN;
    return { ...d, balanceScore };
  });
  const bestBalanced = safeBest(withScores, (d) => d.balanceScore);

  if (bestSigningPerf && bestSigningEnergy) {
    const signingTie = isNearTie(bestSigningPerf, bestSigningEnergy, (d) => d.signing.throughput)
      || bestSigningPerf.algorithm === bestSigningEnergy.algorithm;

    if (signingTie || bestSigningPerf.algorithm === bestSigningEnergy.algorithm) {
      const tieNote = signingTie && bestSigningPerf.algorithm !== bestSigningEnergy.algorithm
        ? ` (performance gap with ${bestSigningEnergy.algorithm} is within 5%)`
        : "";
      html += `
        <div class="recommendation-item">
          <div class="recommendation-title">
            🏆 Best for Signing-Intensive Workloads
            <span class="badge badge-balanced">OPTIMAL</span>
          </div>
          <div class="recommendation-text">
            <strong>${bestSigningPerf.algorithm}</strong> leads in both signing throughput
            (${bestSigningPerf.signing.throughput.toFixed(0)} ops/sec${tieNote}) and CPU Package
            energy efficiency (${bestSigningPerf.signing.energyPkg.toFixed(3)} mJ/op).
            It is the preferred choice for applications where signature generation is the dominant
            operation, such as high-throughput authentication services or signing infrastructure.
          </div>
        </div>`;
    } else {
      html += `
        <div class="recommendation-item">
          <div class="recommendation-title">
            ⚡ Best Signing Throughput
            <span class="badge badge-performance">PERFORMANCE</span>
          </div>
          <div class="recommendation-text">
            <strong>${bestSigningPerf.algorithm}</strong> achieves the highest signing throughput
            at ${bestSigningPerf.signing.throughput.toFixed(0)} ops/sec. This makes it suitable
            for latency-sensitive or high-volume signing workloads where throughput is the primary
            constraint.
          </div>
        </div>`;

      html += `
        <div class="recommendation-item">
          <div class="recommendation-title">
            🌱 Most Energy-Efficient Signing
            <span class="badge badge-energy">SUSTAINABLE</span>
          </div>
          <div class="recommendation-text">
            <strong>${bestSigningEnergy.algorithm}</strong> consumes the least CPU Package energy
            per signing operation (${bestSigningEnergy.signing.energyPkg.toFixed(3)} mJ/op).
            This is advantageous for resource-constrained environments or deployments where energy
            consumption is a key design constraint.
          </div>
        </div>`;
    }
  } else if (bestSigningPerf) {
    html += `
      <div class="recommendation-item">
        <div class="recommendation-title">
          ⚡ Best Signing Throughput
          <span class="badge badge-performance">PERFORMANCE</span>
        </div>
        <div class="recommendation-text">
          <strong>${bestSigningPerf.algorithm}</strong> achieves the highest signing throughput
          at ${bestSigningPerf.signing.throughput.toFixed(0)} ops/sec.
          Energy data was unavailable for a full efficiency comparison.
        </div>
      </div>`;
  }

  if (bestVerifyPerf && bestVerifyEnergy) {
    const verifyTie = isNearTie(bestVerifyPerf, bestVerifyEnergy, (d) => d.verification.throughput)
      || bestVerifyPerf.algorithm === bestVerifyEnergy.algorithm;

    if (verifyTie || bestVerifyPerf.algorithm === bestVerifyEnergy.algorithm) {
      const tieNote = verifyTie && bestVerifyPerf.algorithm !== bestVerifyEnergy.algorithm
        ? ` (performance gap with ${bestVerifyEnergy.algorithm} is within 5%)`
        : "";
      html += `
        <div class="recommendation-item">
          <div class="recommendation-title">
            🏆 Best for Verification-Intensive Workloads
            <span class="badge badge-balanced">OPTIMAL</span>
          </div>
          <div class="recommendation-text">
            <strong>${bestVerifyPerf.algorithm}</strong> leads in both verification throughput
            (${bestVerifyPerf.verification.throughput.toFixed(0)} ops/sec${tieNote}) and CPU
            Package energy efficiency (${bestVerifyPerf.verification.energyPkg.toFixed(3)} mJ/op).
            It is the preferred choice for workloads dominated by signature verification, such as
            distributed systems performing bulk signature validation.
          </div>
        </div>`;
    } else {
      html += `
        <div class="recommendation-item">
          <div class="recommendation-title">
            ⚡ Best Verification Throughput
            <span class="badge badge-performance">PERFORMANCE</span>
          </div>
          <div class="recommendation-text">
            <strong>${bestVerifyPerf.algorithm}</strong> provides the highest verification
            throughput at ${bestVerifyPerf.verification.throughput.toFixed(0)} ops/sec, making it
            well-suited for systems where verification is the bottleneck operation.
          </div>
        </div>`;

      html += `
        <div class="recommendation-item">
          <div class="recommendation-title">
            🌱 Most Energy-Efficient Verification
            <span class="badge badge-energy">SUSTAINABLE</span>
          </div>
          <div class="recommendation-text">
            <strong>${bestVerifyEnergy.algorithm}</strong> uses the least CPU Package energy per
            verification (${bestVerifyEnergy.verification.energyPkg.toFixed(3)} mJ/op), measured
            as idle-subtracted dynamic energy. This is relevant for infrastructure performing
            large volumes of verifications where energy budget is a constraint.
          </div>
        </div>`;
    }
  } else if (bestVerifyPerf) {
    html += `
      <div class="recommendation-item">
        <div class="recommendation-title">
          ⚡ Best Verification Throughput
          <span class="badge badge-performance">PERFORMANCE</span>
        </div>
        <div class="recommendation-text">
          <strong>${bestVerifyPerf.algorithm}</strong> provides the highest verification throughput
          at ${bestVerifyPerf.verification.throughput.toFixed(0)} ops/sec.
          Energy data was unavailable for a full efficiency comparison.
        </div>
      </div>`;
  }

  const PQC_PATTERNS = [/dilithium/i, /falcon/i, /sphincs/i];
  const isPQC = (alg) => PQC_PATTERNS.some((re) => re.test(alg));
  const pqcAlgorithms = comparisonData.filter((d) => isPQC(d.algorithm));
  const classicalAlgorithms = comparisonData.filter((d) => !isPQC(d.algorithm));

  if (pqcAlgorithms.length === 0) {
    html += `
      <div class="recommendation-item">
        <div class="recommendation-title">
          🔐 Quantum-Safe Recommendation
          <span class="badge badge-balanced">POST-QUANTUM</span>
        </div>
        <div class="recommendation-text">
          No post-quantum algorithm selected. To receive a quantum-safe recommendation,
          include benchmark files for Dilithium, Falcon, or SPHINCS+.
        </div>
      </div>`;
  } else {
    const pqcWithScores = pqcAlgorithms.map((d) => {
      const totalThroughput = d.signing.throughput + d.verification.throughput;
      const totalEnergy = d.signing.energyPkg + d.verification.energyPkg;
      const balanceScore =
        Number.isFinite(totalThroughput) && Number.isFinite(totalEnergy) && totalEnergy > 0
          ? totalThroughput / totalEnergy
          : NaN;
      return { ...d, balanceScore };
    });
    const bestPQC = safeBest(pqcWithScores, (d) => d.balanceScore);
    const bestPQCEnergy = safeBest(
      pqcAlgorithms,
      (d) => d.signing.energyPkg + d.verification.energyPkg,
      true
    );

    if (bestPQC) {
      const classicalNote = classicalAlgorithms.length > 0
        ? ` Classical algorithms present in this benchmark (${classicalAlgorithms.map((d) => d.algorithm).join(", ")}) do not provide security against quantum adversaries and should be avoided in threat models that include future quantum attacks.`
        : "";
      const energyNote = bestPQCEnergy && bestPQCEnergy.algorithm !== bestPQC.algorithm
        ? `For deployments where energy is the primary constraint,
           <strong>${bestPQCEnergy.algorithm}</strong> offers the lowest combined CPU Package
           energy among the post-quantum candidates
           (${(bestPQCEnergy.signing.energyPkg + bestPQCEnergy.verification.energyPkg).toFixed(3)} mJ/op).`
        : `It also has the lowest combined CPU Package energy among the post-quantum candidates
           (${(bestPQC.signing.energyPkg + bestPQC.verification.energyPkg).toFixed(3)} mJ/op).`;
      html += `
        <div class="recommendation-item">
          <div class="recommendation-title">
            🔐 Best Quantum-Safe Algorithm
            <span class="badge badge-balanced">POST-QUANTUM</span>
          </div>
          <div class="recommendation-text">
            Among the post-quantum algorithms in this benchmark,
            <strong>${bestPQC.algorithm}</strong> achieves the best performance-to-energy ratio
            (${Number.isFinite(bestPQC.balanceScore) ? bestPQC.balanceScore.toFixed(2) : "—"} ops/sec per mJ).
            ${energyNote}
            All three evaluated algorithms, Dilithium, Falcon, and SPHINCS+, are NIST
            selected post-quantum algorithms and provide security against quantum computing attacks.${classicalNote}
          </div>
        </div>`;
    }
  }

  if (bestOverallEnergy) {
    const totalPkg   = bestOverallEnergy.signing.energyPkg + bestOverallEnergy.verification.energyPkg;
    const totalCores = bestOverallEnergy.signing.energyCores + bestOverallEnergy.verification.energyCores;
    const coresNote  = Number.isFinite(totalCores)
      ? ` The CPU Cores domain (a subset of the Package domain, excluding uncore components) accounts for ${totalCores.toFixed(3)} mJ/op of this total.`
      : "";
    html += `
      <div class="recommendation-item">
        <div class="recommendation-title">
          🌍 Lowest Overall Energy Consumption
          <span class="badge badge-energy">ECO-FRIENDLY</span>
        </div>
        <div class="recommendation-text">
          <strong>${bestOverallEnergy.algorithm}</strong> has the lowest combined CPU Package
          energy across signing and verification
          (${Number.isFinite(totalPkg) ? totalPkg.toFixed(3) : "—"} mJ/op total, idle-subtracted).
          CPU Package energy is reported as the primary metric as it captures the full socket power
          draw, including uncore components.${coresNote}
          This algorithm is preferable in deployments where minimising energy consumption is a
          primary objective.
        </div>
      </div>`;
  }

  if (bestBalanced && Number.isFinite(bestBalanced.balanceScore)) {
    html += `
      <div class="recommendation-item">
        <div class="recommendation-title">
          ⚖️ Best Performance-to-Energy Ratio
          <span class="badge badge-balanced">VERSATILE</span>
        </div>
        <div class="recommendation-text">
          <strong>${bestBalanced.algorithm}</strong> achieves the best ratio of combined throughput
          to combined CPU Package energy (score: ${bestBalanced.balanceScore.toFixed(2)} ops/sec per mJ).
          This makes it a strong general-purpose candidate when neither raw throughput nor minimum
          energy consumption alone drives the selection decision.
        </div>
      </div>`;
  }

  const signingThroughputs = comparisonData.map((d) => d.signing.throughput).filter(Number.isFinite);
  const totalEnergies = comparisonData
    .map((d) => d.signing.energyPkg + d.verification.energyPkg)
    .filter(Number.isFinite);

  const perfRange = signingThroughputs.length >= 2
    ? Math.max(...signingThroughputs) / Math.min(...signingThroughputs)
    : NaN;
  const energyRange = totalEnergies.length >= 2
    ? Math.max(...totalEnergies) / Math.min(...totalEnergies)
    : NaN;

  if ((Number.isFinite(perfRange) && perfRange > 2) || (Number.isFinite(energyRange) && energyRange > 2)) {
    html += `
      <div class="recommendation-item">
        <div class="recommendation-title">
          📊 Key Insights
        </div>
        <div class="recommendation-text">
          ${Number.isFinite(perfRange) && perfRange > 2
            ? `Signing throughput differs by up to ${perfRange.toFixed(1)}× across the evaluated algorithms. `
            : ""}
          ${Number.isFinite(energyRange) && energyRange > 2
            ? `Total CPU Package energy consumption differs by up to ${energyRange.toFixed(1)}× across algorithms. `
            : ""}
          ${Number.isFinite(perfRange) && perfRange > 2 && Number.isFinite(energyRange) && energyRange > 2
            ? "Algorithm choice has a substantial impact on both throughput and energy consumption; selection should be guided by the specific requirements of the target deployment."
            : "Algorithm selection should account for the specific throughput and energy requirements of the target workload."}
        </div>
      </div>`;
  }

  recSection.innerHTML = html;
  container.appendChild(recSection);
}

function calculateStabilityStats() {
  const stats = {
    signing: {
      throughput: calculateMetricStats(parsedData.map((d) => d.signing.throughput)),
      dynamicPkg: calculateMetricStats(parsedData.map((d) => d.signing.dynamicPkg)),
      dynamicCores: calculateMetricStats(parsedData.map((d) => d.signing.dynamicCores)),
    },
    verification: {
      throughput: calculateMetricStats(parsedData.map((d) => d.verification.throughput)),
      dynamicPkg: calculateMetricStats(parsedData.map((d) => d.verification.dynamicPkg)),
      dynamicCores: calculateMetricStats(parsedData.map((d) => d.verification.dynamicCores)),
    },
  };
  return stats;
}

function calculateMetricStats(values) {
  
  const clean = values.filter((v) => Number.isFinite(v));
  const n = clean.length;

  if (n === 0) {
    return {
      n: 0,
      mean: NaN,
      stdDev: NaN,
      cv: NaN,
      min: NaN,
      max: NaN,
      ci95: { low: NaN, high: NaN },
    };
  }

  const mean = clean.reduce((sum, v) => sum + v, 0) / n;

  
  let variance = 0;
  if (n > 1) {
    variance =
      clean.reduce((sum, v) => sum + Math.pow(v - mean, 2), 0) / (n - 1);
  } else {
    variance = 0;
  }

  const stdDev = Math.sqrt(variance);
  const cv = mean !== 0 ? stdDev / mean : NaN;

  const min = Math.min(...clean);
  const max = Math.max(...clean);

  
  const t = n > 1 ? tCritical95(n - 1) : NaN;
  const se = n > 1 ? stdDev / Math.sqrt(n) : NaN;
  const ci95 =
    n > 1
      ? {
          low: mean - t * se,
          high: mean + t * se,
        }
      : { low: NaN, high: NaN };

  return {
    n,
    mean,
    stdDev,
    cv,
    min,
    max,
    ci95,
  };
}

function tCritical95(df) {
  if (!Number.isFinite(df) || df <= 0) return 1.96;
  const tTable = {
    1: 12.706,
    2: 4.303,
    3: 3.182,
    4: 2.776,
    5: 2.571,
    6: 2.447,
    7: 2.365,
    8: 2.306,
    9: 2.262,
    10: 2.228,
    11: 2.201,
    12: 2.179,
    13: 2.16,
    14: 2.145,
    15: 2.131,
    16: 2.12,
    17: 2.11,
    18: 2.101,
    19: 2.093,
    20: 2.086,
    21: 2.08,
    22: 2.074,
    23: 2.069,
    24: 2.064,
    25: 2.06,
    26: 2.056,
    27: 2.052,
    28: 2.048,
    29: 2.045,
    30: 2.042,
  };
  if (df <= 30) return tTable[Math.round(df)] || 1.96;
  return 1.96;
}

function getStabilityIndicator(cv) {
  if (!Number.isFinite(cv)) return '<span class="stability-indicator moderate">N/A</span>';
  if (cv < 0.05)
    return '<span class="stability-indicator stable">Stable</span>';
  if (cv < 0.1)
    return '<span class="stability-indicator moderate">Moderate</span>';
  return '<span class="stability-indicator unstable">Unstable</span>';
}

function createStabilityCharts(container) {
  const throughputChart = document.createElement("div");
  throughputChart.className = "chart-container";
  throughputChart.innerHTML = `
        <div class="chart-header">
          <div class="chart-title">Throughput Stability Across Runs</div>
          <div class="chart-actions">
            <button class="chart-download-btn" onclick="downloadCanvasPNG('throughputChart','throughput_stability')">Download PNG (Hi‑Res)</button>
          </div>
        </div>
        <canvas id="throughputChart"></canvas>
    `;
  container.appendChild(throughputChart);

  const energyChart = document.createElement("div");
  energyChart.className = "chart-container";
  energyChart.innerHTML = `
        <div class="chart-header">
          <div class="chart-title">Energy per Operation Stability (Idle‑Subtracted)</div>
          <div class="chart-actions">
            <button class="chart-download-btn" onclick="downloadCanvasPNG('energyChart','energy_stability')">Download PNG (Hi‑Res)</button>
          </div>
        </div>
        <canvas id="energyChart"></canvas>
    `;
  container.appendChild(energyChart);

  setTimeout(() => {
    const ctx1 = document.getElementById("throughputChart").getContext("2d");
    const chart1 = new Chart(ctx1, {
      type: "line",
      data: {
        labels: parsedData.map((_, i) => `Run ${i + 1}`),
        datasets: [
          {
            label: "Signing (ops/sec)",
            data: parsedData.map((d) => d.signing.throughput),
            borderColor: "#667eea",
            backgroundColor: "rgba(102, 126, 234, 0.1)",
            tension: 0.4,
          },
          {
            label: "Verification (ops/sec)",
            data: parsedData.map((d) => d.verification.throughput),
            borderColor: "#764ba2",
            backgroundColor: "rgba(118, 75, 162, 0.1)",
            tension: 0.4,
          },
        ],
      },
      options: {
        responsive: true,
        scales: {
          y: {
            beginAtZero: true,
            type: isLogScale ? "logarithmic" : "linear",
            title: { display: true, text: "ops/sec" },
          },
          x: { title: { display: true, text: "Run" } },
        },
        plugins: {
          legend: { position: "top" },
          tooltip: { mode: "index", intersect: false },
        },
      },
    });
    currentCharts.push(chart1);

    const ctx2 = document.getElementById("energyChart").getContext("2d");
    const chart2 = new Chart(ctx2, {
      type: "line",
      data: {
        labels: parsedData.map((_, i) => `Run ${i + 1}`),
        datasets: [
          {
            label: "Signing – Package (mJ/op)",
            data: parsedData.map((d) => d.signing.dynamicPkg),
            borderColor: "#56ab2f",
            backgroundColor: "rgba(86, 171, 47, 0.1)",
            tension: 0.4,
          },
          {
            label: "Verification – Package (mJ/op)",
            data: parsedData.map((d) => d.verification.dynamicPkg),
            borderColor: "#a8e6cf",
            backgroundColor: "rgba(168, 230, 207, 0.1)",
            tension: 0.4,
          },
          {
            label: "Signing – Cores (mJ/op)",
            data: parsedData.map((d) => d.signing.dynamicCores),
            borderColor: "#ff9f43",
            backgroundColor: "rgba(255, 159, 67, 0.12)",
            tension: 0.4,
          },
          {
            label: "Verification – Cores (mJ/op)",
            data: parsedData.map((d) => d.verification.dynamicCores),
            borderColor: "#ee5253",
            backgroundColor: "rgba(238, 82, 83, 0.10)",
            tension: 0.4,
          },
        ],
      },
      options: {
        responsive: true,
        scales: {
          y: {
            beginAtZero: true,
            type: isLogScale ? "logarithmic" : "linear",
            title: { display: true, text: "mJ/op" },
          },
          x: { title: { display: true, text: "Run" } },
        },
        plugins: {
          legend: { position: "top" },
          tooltip: { mode: "index", intersect: false },
        },
      },
    });
    currentCharts.push(chart2);
  }, 100);
}

function createComparisonCharts(container, comparisonData) {
  const perfChart = document.createElement("div");
  perfChart.className = "chart-container";
  perfChart.innerHTML = `
        <div class="chart-header">
          <div class="chart-title">Performance Comparison (ops/sec)</div>
          <div class="chart-actions">
            <button class="chart-download-btn" onclick="downloadCanvasPNG('perfChart','performance_comparison')">Download PNG (Hi‑Res)</button>
          </div>
        </div>
        <canvas id="perfChart"></canvas>
    `;
  container.appendChild(perfChart);

  const pkgEnergyChart = document.createElement("div");
  pkgEnergyChart.className = "chart-container";
  pkgEnergyChart.innerHTML = `
        <div class="chart-header">
          <div class="chart-title">Energy per Operation – CPU Package (Idle‑Subtracted, mJ/op)</div>
          <div class="chart-actions">
            <button class="chart-download-btn" onclick="downloadCanvasPNG('energyPkgChart','energy_package_comparison')">Download PNG (Hi‑Res)</button>
          </div>
        </div>
        <canvas id="energyPkgChart"></canvas>
    `;
  container.appendChild(pkgEnergyChart);

  const coresEnergyChart = document.createElement("div");
  coresEnergyChart.className = "chart-container";
  coresEnergyChart.innerHTML = `
        <div class="chart-header">
          <div class="chart-title">Energy per Operation – CPU Cores (Idle‑Subtracted, mJ/op)</div>
          <div class="chart-actions">
            <button class="chart-download-btn" onclick="downloadCanvasPNG('energyCoresChart','energy_cores_comparison')">Download PNG (Hi‑Res)</button>
          </div>
        </div>
        <canvas id="energyCoresChart"></canvas>
    `;
  container.appendChild(coresEnergyChart);

  setTimeout(() => {
    const ctx1 = document.getElementById("perfChart").getContext("2d");
    const chart1 = new Chart(ctx1, {
      type: "bar",
      data: {
        labels: comparisonData.map((d) => d.algorithm),
        datasets: [
          {
            label: "Signing (ops/sec)",
            data: comparisonData.map((d) => d.signing.throughput),
            backgroundColor: "rgba(102, 126, 234, 0.8)",
          },
          {
            label: "Verification (ops/sec)",
            data: comparisonData.map((d) => d.verification.throughput),
            backgroundColor: "rgba(118, 75, 162, 0.8)",
          },
        ],
      },
      options: {
        responsive: true,
        scales: {
          y: {
            beginAtZero: true,
            type: isLogScale ? "logarithmic" : "linear",
            title: { display: true, text: "ops/sec" },
          },
          x: { title: { display: true, text: "Algorithm" } },
        },
        plugins: { legend: { position: "top" } },
      },
    });
    currentCharts.push(chart1);

    const ctx2 = document.getElementById("energyPkgChart").getContext("2d");
    const chart2 = new Chart(ctx2, {
      type: "bar",
      data: {
        labels: comparisonData.map((d) => d.algorithm),
        datasets: [
          {
            label: "Signing (Package)",
            data: comparisonData.map((d) => d.signing.energyPkg),
            backgroundColor: "rgba(86, 171, 47, 0.8)",
          },
          {
            label: "Verification (Package)",
            data: comparisonData.map((d) => d.verification.energyPkg),
            backgroundColor: "rgba(168, 230, 207, 0.8)",
          },
        ],
      },
      options: {
        responsive: true,
        scales: {
          y: {
            beginAtZero: true,
            type: isLogScale ? "logarithmic" : "linear",
            title: { display: true, text: "mJ/op" },
          },
          x: { title: { display: true, text: "Algorithm" } },
        },
        plugins: { legend: { position: "top" } },
      },
    });
    currentCharts.push(chart2);

    const ctx3 = document.getElementById("energyCoresChart").getContext("2d");
    const chart3 = new Chart(ctx3, {
      type: "bar",
      data: {
        labels: comparisonData.map((d) => d.algorithm),
        datasets: [
          {
            label: "Signing (Cores)",
            data: comparisonData.map((d) => d.signing.energyCores),
            backgroundColor: "rgba(255, 159, 67, 0.85)",
          },
          {
            label: "Verification (Cores)",
            data: comparisonData.map((d) => d.verification.energyCores),
            backgroundColor: "rgba(238, 82, 83, 0.80)",
          },
        ],
      },
      options: {
        responsive: true,
        scales: {
          y: {
            beginAtZero: true,
            type: isLogScale ? "logarithmic" : "linear",
            title: { display: true, text: "mJ/op" },
          },
          x: { title: { display: true, text: "Algorithm" } },
        },
        plugins: { legend: { position: "top" } },
      },
    });
    currentCharts.push(chart3);
  }, 100);
}

function createStabilityTable(container) {
  const table = document.createElement("div");
  table.className = "comparison-table";

  let tableHTML = `
        <table>
            <thead>
                <tr>
                    <th>Run</th>
                    <th>Signing Throughput<br>(ops/sec)</th>
                    <th>Verification Throughput<br>(ops/sec)</th>
                    <th>Signing Energy<br>(Package, mJ/op)</th>
                    <th>Verification Energy<br>(Package, mJ/op)</th>
                    <th>Signing Energy<br>(Cores, mJ/op)</th>
                    <th>Verification Energy<br>(Cores, mJ/op)</th>
                </tr>
            </thead>
            <tbody>
    `;

  parsedData.forEach((data) => {
    tableHTML += `
            <tr>
                <td>${data.filename}</td>
                <td>${fmtNumber(data.signing.throughput,2)}</td>
                <td>${fmtNumber(data.verification.throughput,2)}</td>
                <td>${fmtNumber(data.signing.dynamicPkg,3)}</td>
                <td>${fmtNumber(data.verification.dynamicPkg,3)}</td>
                <td>${fmtNumber(data.signing.dynamicCores,3)}</td>
                <td>${fmtNumber(data.verification.dynamicCores,3)}</td>
            </tr>
        `;
  });

  tableHTML += "</tbody></table>";
  table.innerHTML = tableHTML;
  container.appendChild(table);
}

function createComparisonTable(container, comparisonData) {
  const table = document.createElement("div");
  table.className = "comparison-table";

  let tableHTML = `
        <table>
            <thead>
                <tr>
                    <th>Algorithm</th>
                    <th>Runs</th>

                    <th>Signing Perf<br>(ops/sec)</th>
                    <th>Verification Perf<br>(ops/sec)</th>

                    <th>Signing Energy<br>(Package, mJ/op)</th>
                    <th>Verification Energy<br>(Package, mJ/op)</th>

                    <th>Signing Energy<br>(Cores, mJ/op)</th>
                    <th>Verification Energy<br>(Cores, mJ/op)</th>
                </tr>
            </thead>
            <tbody>
    `;

  comparisonData.forEach((data) => {
    tableHTML += `
            <tr>
                <td><strong>${data.algorithm}</strong></td>
                <td>${data.runs}</td>

                <td>${formatMeanSdCi(data.signing.stats.throughput, 2)}</td>
                <td>${formatMeanSdCi(data.verification.stats.throughput, 2)}</td>

                <td>${formatMeanSdCi(data.signing.stats.energyPkg, 3)}</td>
                <td>${formatMeanSdCi(data.verification.stats.energyPkg, 3)}</td>

                <td>${formatMeanSdCi(data.signing.stats.energyCores, 3)}</td>
                <td>${formatMeanSdCi(data.verification.stats.energyCores, 3)}</td>
            </tr>
        `;
  });

  tableHTML += "</tbody></table>";
  table.innerHTML = tableHTML;
  container.appendChild(table);
}

function exportToCSV() {
  let csv = "";

  if (currentAnalysisType === "comparison") {
    const groupedData = {};
    parsedData.forEach((data) => {
      if (!groupedData[data.algorithm]) groupedData[data.algorithm] = [];
      groupedData[data.algorithm].push(data);
    });

    const comparisonStats = Object.keys(groupedData).map((alg) => {
      const runs = groupedData[alg];

      const sThr = calculateMetricStats(runs.map((r) => r.signing.throughput));
      const vThr = calculateMetricStats(runs.map((r) => r.verification.throughput));

      const sPkg = calculateMetricStats(runs.map((r) => r.signing.dynamicPkg));
      const vPkg = calculateMetricStats(runs.map((r) => r.verification.dynamicPkg));

      const sCores = calculateMetricStats(runs.map((r) => r.signing.dynamicCores));
      const vCores = calculateMetricStats(runs.map((r) => r.verification.dynamicCores));

      return { alg, runs: runs.length, sThr, vThr, sPkg, vPkg, sCores, vCores };
    });

    csv =
      "Algorithm,Runs," +
      "Signing Throughput Mean (ops/sec),Signing Throughput SD,Signing Throughput CI Low,Signing Throughput CI High," +
      "Verification Throughput Mean (ops/sec),Verification Throughput SD,Verification Throughput CI Low,Verification Throughput CI High," +
      "Signing Energy Package Mean (mJ/op),Signing Energy Package SD,Signing Energy Package CI Low,Signing Energy Package CI High," +
      "Verification Energy Package Mean (mJ/op),Verification Energy Package SD,Verification Energy Package CI Low,Verification Energy Package CI High," +
      "Signing Energy Cores Mean (mJ/op),Signing Energy Cores SD,Signing Energy Cores CI Low,Signing Energy Cores CI High," +
      "Verification Energy Cores Mean (mJ/op),Verification Energy Cores SD,Verification Energy Cores CI Low,Verification Energy Cores CI High\\n";

    const fmt = (v, d) => (Number.isFinite(v) ? v.toFixed(d) : "");
    const row = (st, d) =>
      `${fmt(st.mean, d)},${fmt(st.stdDev, d)},${fmt(st.ci95.low, d)},${fmt(st.ci95.high, d)}`;

    comparisonStats.forEach((d) => {
      csv +=
        `${d.alg},${d.runs},` +
        `${row(d.sThr, 2)},` +
        `${row(d.vThr, 2)},` +
        `${row(d.sPkg, 3)},` +
        `${row(d.vPkg, 3)},` +
        `${row(d.sCores, 3)},` +
        `${row(d.vCores, 3)}\\n`;
    });
  } else {
    csv =
      "Filename," +
      "Signing Throughput (ops/sec),Verification Throughput (ops/sec)," +
      "Signing Energy Package (mJ/op),Verification Energy Package (mJ/op)," +
      "Signing Energy Cores (mJ/op),Verification Energy Cores (mJ/op)\\n";

    const fmt2 = (v, dec) => (Number.isFinite(v) ? v.toFixed(dec) : "");
    parsedData.forEach((d) => {
      csv +=
        `${d.filename},` +
        `${fmt2(d.signing.throughput, 2)},${fmt2(d.verification.throughput, 2)},` +
        `${fmt2(d.signing.dynamicPkg, 3)},${fmt2(d.verification.dynamicPkg, 3)},` +
        `${fmt2(d.signing.dynamicCores, 3)},${fmt2(d.verification.dynamicCores, 3)}\\n`;
    });
  }

  const blob = new Blob([csv], { type: "text/csv" });
  const url = window.URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = `crypto_benchmark_${currentAnalysisType}_${Date.now()}.csv`;
  a.click();
  window.URL.revokeObjectURL(url);
}

function exportToPDF() {
  const { jsPDF } = window.jspdf;
  const doc = new jsPDF();
  let yPos = 20;

  const ci95 = (stdDev, n) => (n > 1 ? (tCritical95(n - 1) * stdDev) / Math.sqrt(n) : 0);

  doc.setFontSize(18);
  doc.setTextColor(40, 62, 81);
  doc.text("Cryptographic Benchmark Analysis", 105, yPos, { align: "center" });
  yPos += 10;

  doc.setFontSize(11);
  doc.setTextColor(0, 0, 0);
  doc.text(`Generated: ${new Date().toLocaleString()}`, 105, yPos, { align: "center" });
  yPos += 6;

  doc.text(
    `Analysis Type: ${
      currentAnalysisType === "comparison" ? "Algorithm Comparison" : "Stability (Repeatability)"
    }`,
    105,
    yPos,
    { align: "center" }
  );
  yPos += 12;

  
  doc.setFontSize(10);
  doc.setTextColor(80, 80, 80);
  doc.text(
    "Energy metrics are reported as idle-subtracted dynamic energy per operation (mJ/op) using RAPL domains.",
    20,
    yPos
  );
  yPos += 6;
  doc.text(
    "We report both CPU Package and CPU Cores. For repeat runs, mean ± 95% CI and coefficient of variation (CV) are included.",
    20,
    yPos
  );
  yPos += 12;

  doc.setTextColor(0, 0, 0);

  if (currentAnalysisType === "comparison") {
    const groupedData = {};
    parsedData.forEach((d) => {
      if (!groupedData[d.algorithm]) groupedData[d.algorithm] = [];
      groupedData[d.algorithm].push(d);
    });

    const comparisonData = Object.keys(groupedData).map((alg) => {
      const runs = groupedData[alg];
      return {
        algorithm: alg,
        runs: runs.length,
        signing: {
          throughput: runs.reduce((sum, r) => sum + r.signing.throughput, 0) / runs.length,
          energyPkg: runs.reduce((sum, r) => sum + r.signing.dynamicPkg, 0) / runs.length,
          energyCores: runs.reduce((sum, r) => sum + r.signing.dynamicCores, 0) / runs.length,
        },
        verification: {
          throughput: runs.reduce((sum, r) => sum + r.verification.throughput, 0) / runs.length,
          energyPkg: runs.reduce((sum, r) => sum + r.verification.dynamicPkg, 0) / runs.length,
          energyCores: runs.reduce((sum, r) => sum + r.verification.dynamicCores, 0) / runs.length,
        },
      };
    });

    const bestSigning = comparisonData.reduce((best, curr) =>
      curr.signing.throughput > best.signing.throughput ? curr : best
    );
    const bestVerification = comparisonData.reduce((best, curr) =>
      curr.verification.throughput > best.verification.throughput ? curr : best
    );
    const mostEfficientPkg = comparisonData.reduce((best, curr) =>
      curr.signing.energyPkg + curr.verification.energyPkg <
      best.signing.energyPkg + best.verification.energyPkg
        ? curr
        : best
    );

    doc.setFontSize(13);
    doc.setTextColor(40, 62, 81);
    doc.text("Summary", 20, yPos);
    yPos += 8;

    doc.setFontSize(10);
    doc.setTextColor(0, 0, 0);
    doc.text(
      `Best Signing Performance: ${bestSigning.algorithm} (${bestSigning.signing.throughput.toFixed(0)} ops/sec)`,
      20,
      yPos
    );
    yPos += 6;
    doc.text(
      `Best Verification Performance: ${bestVerification.algorithm} (${bestVerification.verification.throughput.toFixed(0)} ops/sec)`,
      20,
      yPos
    );
    yPos += 6;
    doc.text(
      `Most Energy Efficient (CPU Package): ${mostEfficientPkg.algorithm} (${(
        mostEfficientPkg.signing.energyPkg + mostEfficientPkg.verification.energyPkg
      ).toFixed(3)} mJ/op)`,
      20,
      yPos
    );
    yPos += 12;

    doc.setFontSize(13);
    doc.setTextColor(40, 62, 81);
    doc.text("Per-Algorithm Results (Averages)", 20, yPos);
    yPos += 8;

    doc.setFontSize(9);
    doc.setTextColor(0, 0, 0);

    comparisonData.forEach((d) => {
      if (yPos > 270) {
        doc.addPage();
        yPos = 20;
      }

      const totalPkg = d.signing.energyPkg + d.verification.energyPkg;
      const totalCores = d.signing.energyCores + d.verification.energyCores;

      doc.setFont(undefined, "bold");
      doc.text(`${d.algorithm}  (runs: ${d.runs})`, 20, yPos);
      doc.setFont(undefined, "normal");
      yPos += 6;

      doc.text(
        `Signing: ${d.signing.throughput.toFixed(2)} ops/sec | ${d.signing.energyPkg.toFixed(
          3
        )} mJ/op (Pkg) | ${d.signing.energyCores.toFixed(3)} mJ/op (Cores)`,
        20,
        yPos
      );
      yPos += 6;
      doc.text(
        `Verification: ${d.verification.throughput.toFixed(
          2
        )} ops/sec | ${d.verification.energyPkg.toFixed(3)} mJ/op (Pkg) | ${d.verification.energyCores.toFixed(
          3
        )} mJ/op (Cores)`,
        20,
        yPos
      );
      yPos += 6;
      doc.text(
        `Total energy: ${totalPkg.toFixed(3)} mJ/op (Pkg) | ${totalCores.toFixed(3)} mJ/op (Cores)`,
        20,
        yPos
      );
      yPos += 10;
    });
  } else {
    const stats = calculateStabilityStats();
    const algorithm = parsedData[0].algorithm;
    const n = parsedData.length;

    doc.setFontSize(13);
    doc.setTextColor(40, 62, 81);
    doc.text(`Algorithm: ${algorithm}`, 20, yPos);
    yPos += 8;

    doc.setFontSize(10);
    doc.setTextColor(0, 0, 0);
    doc.text(`Number of runs: ${n}`, 20, yPos);
    yPos += 10;

    
    doc.setFontSize(12);
    doc.setTextColor(40, 62, 81);
    doc.text("Throughput", 20, yPos);
    yPos += 7;

    doc.setFontSize(10);
    doc.setTextColor(0, 0, 0);
    doc.text(
      `Signing: ${fmtNumber(stats.signing.throughput.mean,2)} ± ${ci95(
        stats.signing.throughput.stdDev,
        n
      ).toFixed(2)} ops/sec (95% CI), CV ${Number.isFinite(stats.signing.throughput.cv)?(stats.signing.throughput.cv*100).toFixed(2)+'%':'—'}`,
      20,
      yPos
    );
    yPos += 6;
    doc.text(
      `Verification: ${fmtNumber(stats.verification.throughput.mean,2)} ± ${ci95(
        stats.verification.throughput.stdDev,
        n
      ).toFixed(2)} ops/sec (95% CI), CV ${Number.isFinite(stats.verification.throughput.cv)?(stats.verification.throughput.cv*100).toFixed(2)+'%':'—'}`,
      20,
      yPos
    );
    yPos += 10;

    
    doc.setFontSize(12);
    doc.setTextColor(40, 62, 81);
    doc.text("Energy per Operation (Idle-Subtracted)", 20, yPos);
    yPos += 7;

    doc.setFontSize(10);
    doc.setTextColor(0, 0, 0);
    doc.text(
      `Signing (Pkg): ${fmtNumber(stats.signing.dynamicPkg.mean,3)} ± ${ci95(
        stats.signing.dynamicPkg.stdDev,
        n
      ).toFixed(3)} mJ/op, CV ${Number.isFinite(stats.signing.dynamicPkg.cv)?(stats.signing.dynamicPkg.cv*100).toFixed(2)+'%':'—'}`,
      20,
      yPos
    );
    yPos += 6;
    doc.text(
      `Verification (Pkg): ${fmtNumber(stats.verification.dynamicPkg.mean,3)} ± ${ci95(
        stats.verification.dynamicPkg.stdDev,
        n
      ).toFixed(3)} mJ/op, CV ${Number.isFinite(stats.verification.dynamicPkg.cv)?(stats.verification.dynamicPkg.cv*100).toFixed(2)+'%':'—'}`,
      20,
      yPos
    );
    yPos += 6;
    doc.text(
      `Signing (Cores): ${fmtNumber(stats.signing.dynamicCores.mean,3)} ± ${ci95(
        stats.signing.dynamicCores.stdDev,
        n
      ).toFixed(3)} mJ/op, CV ${Number.isFinite(stats.signing.dynamicCores.cv)?(stats.signing.dynamicCores.cv*100).toFixed(2)+'%':'—'}`,
      20,
      yPos
    );
    yPos += 6;
    doc.text(
      `Verification (Cores): ${fmtNumber(stats.verification.dynamicCores.mean,3)} ± ${ci95(
        stats.verification.dynamicCores.stdDev,
        n
      ).toFixed(3)} mJ/op, CV ${Number.isFinite(stats.verification.dynamicCores.cv)?(stats.verification.dynamicCores.cv*100).toFixed(2)+'%':'—'}`,
      20,
      yPos
    );
  }

  doc.save(`crypto_benchmark_${currentAnalysisType}_${Date.now()}.pdf`);
}

function formatMeanSdCi(stats, decimals = 3) {
  if (!stats || !Number.isFinite(stats.mean)) return "—";
  const mean = stats.mean.toFixed(decimals);
  const sd = Number.isFinite(stats.stdDev) ? stats.stdDev.toFixed(decimals) : "0";
  let html = `${mean} ± ${sd}`;
  if (stats.n && stats.n > 1 && stats.ci95 && Number.isFinite(stats.ci95.low)) {
    html += `<br><span style="font-size:11px;color:#666;">95% CI: [${stats.ci95.low.toFixed(decimals)}, ${stats.ci95.high.toFixed(decimals)}]</span>`;
  }
  return html;
}

function fmtNumber(v, dec = 2) {
  return Number.isFinite(v) ? v.toFixed(dec) : "—";
}
