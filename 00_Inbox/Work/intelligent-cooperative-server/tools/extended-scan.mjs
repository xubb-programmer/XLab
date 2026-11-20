import fs from 'node:fs';
import path from 'node:path';

// 检查工程源码、注释、文档、配置以及文件名、目录名中是否存在任何军事用语
const regex = /(军事|作战|单兵|战场|战区|战术|战备|战机|战斗|战损|火力|打击|弹药|射击|毁伤|摧毁|歼灭|敌方|我方|友军|敌军|红军|蓝军|阵地|阵位|部队|兵力|防空|炮兵|火炮|导弹|侦察|侦查|靶标|靶机|武器|突击|射程|齐射|校射|炮击|雷达|引导打击|指挥中枢|指挥所|炸点|营级|连级|各营|所属营|所属连|个营|个连|营连|建制|military)/i;
const filenameRegex = /(军事|作战|单兵|战场|战区|战术|战备|战机|战斗|战损|火力|打击|弹药|射击|毁伤|摧毁|歼灭|敌方|我方|友军|敌军|红军|蓝军|阵地|阵位|部队|兵力|防空|炮兵|火炮|导弹|侦察|侦查|靶标|靶机|武器|突击|射程|齐射|校射|炮击|雷达|引导打击|指挥中枢|指挥所|炸点|military|combat|artillery|firepower|reconnaissance|\brecon\b|strike range|tactical)/i;

const dirs = ['src', 'server', 'config', 'public', 'tools', 'docs'];
const rootFiles = ['package.json', 'README.md', 'REQUIREMENTS.md'];

const matches = [];
const nameMatches = [];

function checkFileContent(file) {
  const content = fs.readFileSync(file, 'utf8');
  const lines = content.split('\n');
  lines.forEach((line, idx) => {
    const m = line.match(regex);
    if (m) {
      matches.push({ file, line: idx + 1, term: m[0], text: line.trim() });
    }
  });
}

function walk(dir) {
  if (!fs.existsSync(dir)) return;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (['node_modules', '.git', 'dist'].includes(entry.name)) continue;

    if (entry.name !== 'extended-scan.mjs' && filenameRegex.test(entry.name)) {
      nameMatches.push({ type: entry.isDirectory() ? 'DIR' : 'FILE', path: full, name: entry.name });
    }

    if (entry.isDirectory()) {
      walk(full);
    } else if (/\.(js|mjs|html|css|json|sh|service|desktop|md)$/.test(entry.name)) {
      if (entry.name !== 'extended-scan.mjs') {
        checkFileContent(full);
      }
    }
  }
}

for (const d of dirs) walk(d);
for (const rf of rootFiles) {
  if (fs.existsSync(rf)) checkFileContent(rf);
}

console.log('--- 扫描验证结果 ---');
console.log(`文件名/目录名违规匹配数: ${nameMatches.length}`);
if (nameMatches.length > 0) {
  nameMatches.forEach(nm => console.log(`  [${nm.type}] ${nm.path}`));
}

console.log(`文件内容/注释违规匹配数: ${matches.length}`);
if (matches.length > 0) {
  matches.forEach(m => console.log(`  ${m.file}:${m.line} [${m.term}] ${m.text}`));
}

if (nameMatches.length === 0 && matches.length === 0) {
  console.log('✓ 全工程源码、注释、文档、文件名、目录名完全合规（0 军事敏感词）！');
}
