/**
 * 火力打击多目标对多火力点协同分配策略计算引擎 (人机在环 · 建议预览)
 * 核心准则：
 * 1. 射程包络硬约束 (D <= R)
 * 2. 距离最近原则 (min Distance / 弹道耗时短 / 散布小)
 * 3. 多阵位协同负载均衡 (避免单阵位通道阻塞，促成并发齐射)
 */

/**
 * 计算地球表面两经纬度点之间的大圆距离 (单位: km)
 */
export function calculateDistanceKm(lat1, lon1, lat2, lon2) {
  if (lat1 == null || lon1 == null || lat2 == null || lon2 == null) return 0;
  const R = 6371.0088; // 地球平均半径 (km)
  const phi1 = (Number(lat1) * Math.PI) / 180;
  const phi2 = (Number(lat2) * Math.PI) / 180;
  const deltaPhi = ((Number(lat2) - Number(lat1)) * Math.PI) / 180;
  const deltaLambda = ((Number(lon2) - Number(lon1)) * Math.PI) / 180;

  const a =
    Math.sin(deltaPhi / 2) * Math.sin(deltaPhi / 2) +
    Math.cos(phi1) * Math.cos(phi2) * Math.sin(deltaLambda / 2) * Math.sin(deltaLambda / 2);
  const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
  return R * c;
}

/**
 * 解析打击范围为标准千米数 (km)
 */
export function parseStrikeRangeKm(rangeVal) {
  if (typeof rangeVal === "number") {
    return rangeVal > 500 ? rangeVal / 1000 : rangeVal;
  }
  if (typeof rangeVal === "string" && rangeVal.trim() !== "") {
    const match = rangeVal.match(/(\d+(?:\.\d+)?)\s*(km|千米|m|米)?/i);
    if (match) {
      const num = parseFloat(match[1]);
      const unit = (match[2] || "").toLowerCase();
      return unit.includes("m") && !unit.includes("k") && !unit.includes("千") ? num / 1000 : num;
    }
  }
  return 40; // 默认 40 km
}

/**
 * 计算单个目标相对于某个火力点的空间与包络关系
 */
export function evaluateTargetToPoint(target, point) {
  const strikeRangeKm = parseStrikeRangeKm(point.strikeRange ?? point.maxRangeKm);
  const hasPos = typeof point.latitude === "number" && typeof point.longitude === "number";

  if (!hasPos) {
    return {
      point,
      hasPosition: false,
      distanceKm: 0,
      strikeRangeKm,
      inRange: false,
      marginKm: 0,
      statusLabel: "阵位坐标未配置"
    };
  }

  const distanceKm = Number(
    calculateDistanceKm(point.latitude, point.longitude, target.latitude, target.longitude).toFixed(1)
  );
  const inRange = distanceKm <= strikeRangeKm;
  const marginKm = Number((strikeRangeKm - distanceKm).toFixed(1));
  const statusLabel = inRange
    ? `射程内 (${distanceKm}km / 最大${strikeRangeKm}km)`
    : `超射程 ${Math.abs(marginKm)}km`;

  return {
    point,
    hasPosition: true,
    distanceKm,
    strikeRangeKm,
    inRange,
    marginKm,
    statusLabel
  };
}

/**
 * 多目标对多火力点协同分配策略解算 (Multi-Target to Multi-Battery Allocation)
 * @param {Array} targets 选中的目标列表
 * @param {Array} firepowerPoints 在席火力点列表
 * @returns {Object} 包含按目标分配建议、阵位负载汇总与战术说明的方案对象
 */
export function computeMultiAllocationPlan(targets, firepowerPoints) {
  if (!targets || targets.length === 0 || !firepowerPoints || firepowerPoints.length === 0) {
    return {
      hasPlan: false,
      targetPlans: [],
      batteryLoads: {},
      overallRationale: "无待分配目标或无可用火力点"
    };
  }

  const validPoints = firepowerPoints.filter(p => typeof p.latitude === "number" && typeof p.longitude === "number");
  const batteryLoads = new Map();
  const batteryCapacities = new Map();
  for (const p of firepowerPoints) {
    batteryLoads.set(p.id, 0);
    batteryCapacities.set(p.id, Math.max(1, Number(p.maxTargets) || 1));
  }

  // 1. 为每个目标解算所有可用阵位的距离与包络
  const targetCandidateList = targets.map(target => {
    const candidates = validPoints.map(point => {
      const evalData = evaluateTargetToPoint(target, point);
      return {
        point,
        ...evalData
      };
    });

    // 候选阵位排序规则：优先在射程内；在射程内按距离升序；超出射程按超距升序
    candidates.sort((a, b) => {
      if (a.inRange !== b.inRange) {
        return a.inRange ? -1 : 1;
      }
      return a.distanceKm - b.distanceKm;
    });

    return {
      target,
      candidates
    };
  });

  // 2. 考虑阵位最大目标容量上限 (maxTargets) 与多阵位协同均衡，进行目标到阵位的匹配指派
  const targetPlans = [];

  for (const item of targetCandidateList) {
    const target = item.target;
    const candidates = item.candidates;

    if (candidates.length === 0) {
      targetPlans.push({
        target,
        recommendedPointId: firepowerPoints[0]?.id || "FIRE-01",
        recommendedPoint: firepowerPoints[0] || null,
        recommendedDistanceKm: 0,
        recommendedInRange: false,
        recommendedReason: "未配置有效阵位坐标",
        evaluations: []
      });
      continue;
    }

    // 候选阵位智能遴选：
    // 优先层：射程内 且 承揽数未达上限 (batteryLoads < maxTargets) 的阵位
    const inRangeUnderCap = candidates.filter(
      c => c.inRange && (batteryLoads.get(c.point.id) || 0) < (batteryCapacities.get(c.point.id) || 1)
    );

    let chosen;
    if (inRangeUnderCap.length > 0) {
      chosen = inRangeUnderCap[0];
      // 如果有多处未满载候选阵位，且最优候选负载已接近其上限，次优候选在射程内且距离相近(<=35%)但剩余容量更充裕，则分流促成齐射
      if (inRangeUnderCap.length > 1) {
        const bestLoad = batteryLoads.get(chosen.point.id) || 0;
        const bestCap = batteryCapacities.get(chosen.point.id) || 1;
        const alt = inRangeUnderCap.find(c => {
          const cLoad = batteryLoads.get(c.point.id) || 0;
          const cCap = batteryCapacities.get(c.point.id) || 1;
          return (cCap - cLoad) > (bestCap - bestLoad) && c.distanceKm <= chosen.distanceKm * 1.35;
        });
        if (alt) chosen = alt;
      }
    } else {
      // 若射程内阵位均已满载：优先选射程内的阵位中超额最少且相对最近的；若全超射程则选最近阵位
      const inRangeOverCap = candidates.filter(c => c.inRange);
      if (inRangeOverCap.length > 0) {
        inRangeOverCap.sort((a, b) => {
          const loadA = batteryLoads.get(a.point.id) || 0;
          const capA = batteryCapacities.get(a.point.id) || 1;
          const loadB = batteryLoads.get(b.point.id) || 0;
          const capB = batteryCapacities.get(b.point.id) || 1;
          const diffA = loadA - capA;
          const diffB = loadB - capB;
          if (diffA !== diffB) return diffA - diffB;
          return a.distanceKm - b.distanceKm;
        });
        chosen = inRangeOverCap[0];
      } else {
        chosen = candidates[0];
      }
    }

    // 更新阵位负载计数
    const chosenId = chosen.point.id;
    const currentLoadBefore = batteryLoads.get(chosenId) || 0;
    const capacity = batteryCapacities.get(chosenId) || 1;
    batteryLoads.set(chosenId, currentLoadBefore + 1);
    const isOverCapacity = (currentLoadBefore + 1) > capacity;

    // 生成战术理由
    let reason = "";
    if (chosen.inRange) {
      if (isOverCapacity) {
        reason = `在打击射程包络内（距目标 ${chosen.distanceKm} km ≤ ${chosen.strikeRangeKm} km），但该阵位已达单次最大目标容量上限（已分配 ${currentLoadBefore + 1}/${capacity} 个），建议核审分流或分批打击。`;
      } else {
        reason = `在打击射程包络内（距目标 ${chosen.distanceKm} km ≤ ${chosen.strikeRangeKm} km），通道容量充裕（已分配 ${currentLoadBefore + 1}/${capacity} 个），飞行耗时短、散布精度高。`;
      }
    } else {
      reason = `超出所有可用阵位射程，该阵位相对最近（超距 ${Math.abs(chosen.marginKm)} km）。`;
    }

    // 生成供该目标专用的下拉选项列表
    const evaluations = candidates.map(c => {
      const isRec = c.point.id === chosenId;
      const recTag = isRec ? " ★ 系统建议" : "";
      const cCap = batteryCapacities.get(c.point.id) || 1;
      return {
        pointId: c.point.id,
        pointName: c.point.name || c.point.id,
        distanceKm: c.distanceKm,
        inRange: c.inRange,
        marginKm: c.marginKm,
        maxTargets: cCap,
        isRecommended: isRec,
        optionText: `${c.point.name} · ${c.statusLabel} [容量上限: ${cCap}]${recTag}`
      };
    });

    targetPlans.push({
      target,
      recommendedPointId: chosenId,
      recommendedPoint: chosen.point,
      recommendedDistanceKm: chosen.distanceKm,
      recommendedInRange: chosen.inRange,
      isOverCapacity,
      maxTargets: capacity,
      recommendedReason: reason,
      evaluations
    });
  }

  // 3. 生成整体协同态势战术说明
  const loadSummaryParts = [];
  let inRangeCount = 0;
  for (const plan of targetPlans) {
    if (plan.recommendedInRange) inRangeCount++;
  }
  let hasAnyOverload = false;
  for (const [id, count] of batteryLoads) {
    const pt = firepowerPoints.find(p => p.id === id);
    const cap = batteryCapacities.get(id) || 1;
    if (pt && count > 0) {
      const isOver = count > cap;
      if (isOver) hasAnyOverload = true;
      loadSummaryParts.push(`【${pt.name}】${count}/${cap} 个${isOver ? '⚠️' : ''}`);
    }
  }

  const baseRatioText = inRangeCount === targets.length
    ? `推荐就近分流（${targets.length} 目标均在射程内）`
    : `就近分流（${inRangeCount}/${targets.length} 目标在射程内）`;

  const overallRationale = hasAnyOverload
    ? `${baseRatioText} · 存在阵位超出通道容量上限`
    : `${baseRatioText} · 各阵位容量受控`;

  const loadsObj = {};
  for (const [id, count] of batteryLoads) {
    loadsObj[id] = count;
  }
  const capsObj = {};
  for (const [id, cap] of batteryCapacities) {
    capsObj[id] = cap;
  }

  return {
    hasPlan: true,
    targetPlans,
    batteryLoads: loadsObj,
    batteryCapacities: capsObj,
    overallRationale
  };
}

/**
 * 兼容原有单一推荐接口
 */
export function evaluateAllocationStrategy(targets, firepowerPoints) {
  const multiPlan = computeMultiAllocationPlan(targets, firepowerPoints);
  const bestPointId = multiPlan.targetPlans[0]?.recommendedPointId || firepowerPoints[0]?.id;
  const bestPoint = firepowerPoints.find(p => p.id === bestPointId) || firepowerPoints[0];

  return {
    hasRecommendation: multiPlan.hasPlan,
    recommendedPointId: bestPointId,
    recommendedPoint: bestPoint,
    recommendationReason: multiPlan.overallRationale,
    pointEvaluations: multiPlan.targetPlans[0]?.evaluations || [],
    multiPlan
  };
}
