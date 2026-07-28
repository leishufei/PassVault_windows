import { useMemo, useState } from "react";
import {
  Archive,
  ArrowLeft,
  Check,
  ChevronDown,
  Cloud,
  Copy,
  ExternalLink,
  Eye,
  EyeOff,
  Folder,
  FolderOpen,
  KeyRound,
  List,
  LockKeyhole,
  MoreHorizontal,
  Pencil,
  Plus,
  Search,
  Settings2,
  ShieldAlert,
  ShieldCheck,
  Star,
  Trash2,
  Vault,
  X,
} from "lucide-react";

type VaultItem = { id: number; name: string; account: string; domain: string; tag: string; icon: string; color: string; changed: string; risky?: boolean };

const items: VaultItem[] = [
  { id: 1, name: "Google", account: "leishufei@gmail.com", domain: "accounts.google.com", tag: "工作", icon: "G", color: "bg-[#f7f8fa] text-[#4285f4]", changed: "3 分钟前" },
  { id: 2, name: "GitHub", account: "leishufei", domain: "github.com", tag: "工作", icon: "GH", color: "bg-[#202725] text-white", changed: "10 分钟前" },
  { id: 3, name: "Microsoft", account: "leishufei@outlook.com", domain: "login.live.com", tag: "个人", icon: "M", color: "bg-[#ebf3ff] text-[#2577ce]", changed: "1 小时前" },
  { id: 4, name: "支付宝", account: "138****1254", domain: "alipay.com", tag: "金融", icon: "支", color: "bg-[#1677ff] text-white", changed: "2 小时前" },
  { id: 5, name: "微信", account: "leishufei_2020", domain: "weixin.qq.com", tag: "个人", icon: "微", color: "bg-[#20b85a] text-white", changed: "5 小时前" },
  { id: 6, name: "Amazon", account: "leishufei@gmail.com", domain: "amazon.com", tag: "购物", icon: "a", color: "bg-[#1e2930] text-[#f7ad2d]", changed: "2 天前", risky: true },
  { id: 7, name: "Notion", account: "leishufei", domain: "notion.so", tag: "工作", icon: "N", color: "bg-white text-[#171717]", changed: "3 天前" },
];

const sections = [
  ["全部密码", "128"], ["收藏夹", "12"], ["未分类", ""], ["回收站", ""],
];
const groups = [["工作", "42"], ["个人", "36"], ["金融", "18"], ["社交", "16"], ["学习", "16"]];

export default function App() {
  const [page, setPage] = useState<"vault" | "preferences">("vault");
  const [active, setActive] = useState("全部密码");
  const [selectedId, setSelectedId] = useState(1);
  const [editorMode, setEditorMode] = useState<"edit" | "create" | null>(null);
  const [query, setQuery] = useState("");
  const [showPassword, setShowPassword] = useState(false);
  const [copied, setCopied] = useState(false);
  const [prefTab, setPrefTab] = useState("常规");
  const selected = items.find((item) => item.id === selectedId) ?? items[0];
  const list = useMemo(() => items.filter((item) => `${item.name}${item.account}${item.domain}`.toLowerCase().includes(query.toLowerCase())), [query]);
  const choose = (id: number) => { setSelectedId(id); setShowPassword(false); };
  const copy = () => { navigator.clipboard?.writeText("Dawn!Harbor_2026"); setCopied(true); setTimeout(() => setCopied(false), 1200); };

  return <main className="min-h-screen bg-[#eef2f7] p-2 font-['Noto_Sans_SC'] text-[#1e2a38] sm:p-4">
    <div className="mx-auto flex min-h-[760px] max-w-[1600px] overflow-hidden rounded-[22px] border border-[#d9e1eb] bg-[#fbfcfe] shadow-[0_24px_70px_rgba(52,72,99,0.12)]">
      {page === "vault" && <Sidebar page={page} active={active} setActive={setActive} onVault={() => setPage("vault")} onSettings={() => setPage("preferences")} onCreate={() => setEditorMode("create")} />}
      {page === "vault" ? <VaultWorkspace list={list} selected={selected} selectedId={selectedId} query={query} setQuery={setQuery} choose={choose} openEditor={() => setEditorMode("edit")} showPassword={showPassword} setShowPassword={setShowPassword} copy={copy} copied={copied} /> : <Preferences tab={prefTab} setTab={setPrefTab} onBack={() => setPage("vault")} />}
      {editorMode && <Editor item={editorMode === "edit" ? selected : undefined} onClose={() => setEditorMode(null)} />}
    </div>
  </main>;
}

function Sidebar({ page, active, setActive, onVault, onSettings, onCreate }: { page: string; active: string; setActive: (value: string) => void; onVault: () => void; onSettings: () => void; onCreate: () => void }) {
  const sectionIcons = [List, Star, FolderOpen, Trash2];
  return <aside className="hidden w-[224px] shrink-0 flex-col border-r border-[#e3e8ef] bg-[#f5f8fc] px-3 py-5 lg:flex">
    <div className="mb-8 flex items-center gap-2.5 px-3"><div className="grid size-8 place-items-center rounded-[10px] bg-[#276cf0] shadow-[inset_0_0_0_1px_rgba(255,255,255,0.25)]"><Vault className="size-[17px] text-white" /></div><span className="text-[17px] font-bold tracking-[-0.03em]">PassVault</span></div>
    <button onClick={onCreate} title="新建密码" className="mb-5 flex h-10 items-center justify-center gap-2 rounded-lg bg-[#276cf0] text-sm font-semibold text-white shadow-[0_5px_14px_rgba(39,108,240,0.25)] transition hover:bg-[#1d60df] active:scale-[0.98]"><Plus className="size-[17px] stroke-[2.4]" />新建密码</button>
    <nav className="space-y-1">{sections.map(([label, count], index) => { const Icon = sectionIcons[index]; return <button key={label} title={label} onClick={() => { onVault(); setActive(label); }} className={`flex h-9 w-full items-center gap-3 rounded-lg px-3 text-sm transition ${page === "vault" && active === label ? "bg-[#e8f0ff] font-semibold text-[#2369ee]" : "text-[#59687a] hover:bg-[#eaf0f8]"}`}><Icon className="size-[16px] shrink-0 stroke-[1.8]" />{label}{count && <span className="ml-auto rounded-full bg-[#d8e7ff] px-1.5 py-px text-[10px] font-semibold text-[#3675de]">{count}</span>}</button>; })}</nav>
    <div className="my-5 h-px bg-[#e2e8f0]" />
    <div className="mb-2 flex items-center justify-between px-3"><span className="font-['DM_Mono'] text-[10px] uppercase tracking-[0.13em] text-[#8b98a8]">分类</span><button title="新增分类" className="grid size-6 place-items-center rounded-md text-[#617084] transition hover:bg-[#e1eaf7] hover:text-[#276cf0]"><Plus className="size-3.5 stroke-[2.2]" /></button></div>
    <nav className="space-y-1">{groups.map(([label,count]) => <button key={label} onClick={() => { onVault(); setActive(label); }} className={`flex h-8 w-full items-center gap-3 rounded-lg px-3 text-sm transition ${active === label && page === "vault" ? "bg-[#e8f0ff] font-semibold text-[#2369ee]" : "text-[#667587] hover:bg-[#eaf0f8]"}`}><Folder className="size-[15px] stroke-[1.8]" />{label}<span className="ml-auto rounded-full bg-[#e3ebf7] px-1.5 py-px text-[10px] text-[#6e7e91]">{count}</span></button>)}</nav>
    <div className="mt-auto border-t border-[#e2e8f0] pt-3"><div className="mb-2 flex h-9 items-center gap-3 rounded-lg px-3 text-xs text-[#718197]"><Cloud className="size-[16px] shrink-0 text-[#4281f2]" /><span className="min-w-0 flex-1 truncate">已同步到云端</span><span className="size-1.5 rounded-full bg-[#34a36a]" /></div><button onClick={onSettings} title="偏好设置" className="flex h-9 w-full items-center gap-3 rounded-lg px-3 text-sm text-[#667587] transition hover:bg-[#eaf0f8]"><Settings2 className="size-[16px] stroke-[1.8]" />设置</button></div>
  </aside>;
}

function VaultWorkspace({ list, selected, selectedId, query, setQuery, choose, openEditor, showPassword, setShowPassword, copy, copied }: { list: VaultItem[]; selected: VaultItem; selectedId: number; query: string; setQuery: (s: string) => void; choose: (id: number) => void; openEditor: () => void; showPassword: boolean; setShowPassword: (x: boolean) => void; copy: () => void; copied: boolean }) {
  return <section className="flex min-w-0 flex-1 flex-col"><header className="flex h-[68px] shrink-0 items-center border-b border-[#e4e9ef] px-5 sm:px-7"><div className="flex h-9 max-w-[390px] flex-1 items-center gap-2.5 rounded-lg border border-[#dce4ee] bg-[#f7f9fc] px-3 text-[#768599] focus-within:border-[#8ab3ff] focus-within:ring-2 focus-within:ring-[#dbe9ff]"><Search className="size-4"/><input value={query} onChange={(e)=>setQuery(e.target.value)} className="min-w-0 flex-1 bg-transparent text-xs outline-none placeholder:text-[#8a98aa]" placeholder="搜索标题、用户名、网址或备注"/><span className="rounded bg-white px-1.5 py-0.5 font-['DM_Mono'] text-[9px] text-[#8694a5]">Ctrl + F</span></div><div className="ml-auto flex items-center gap-1.5"><button title="锁定保险库" className="grid size-9 place-items-center rounded-lg text-[#3b78df] transition hover:bg-[#eaf2ff]"><LockKeyhole className="size-[18px] stroke-[2]"/></button><div className="mx-1 h-5 border-l border-[#e1e6ed]"/><button title="更多操作" className="grid size-9 place-items-center rounded-lg text-[#6b7b8f] transition hover:bg-[#eef3f8]"><MoreHorizontal className="size-5 stroke-[2]" /></button></div></header><div className="grid min-h-0 flex-1 grid-cols-1 xl:grid-cols-[minmax(430px,1fr)_minmax(360px,430px)]"><section className="border-b border-[#e5eaf0] p-5 xl:border-b-0 xl:border-r xl:p-7"><div className="mb-4 flex items-center"><h1 className="text-base font-bold">所有密码</h1><span className="ml-3 text-xs font-medium text-[#3777e7]">128 项</span><button className="ml-auto flex items-center gap-1 text-[11px] text-[#7e8c9b]">按更新时间 <ChevronDown className="size-3" /></button></div><div className="space-y-1.5">{list.map((item) => <button key={item.id} onClick={() => choose(item.id)} className={`flex w-full items-center gap-3 rounded-xl border p-3 text-left transition ${selectedId === item.id ? "border-[#79a9ff] bg-[#f4f8ff] shadow-[0_3px_10px_rgba(53,105,193,0.09)]" : "border-[#edf0f4] bg-white hover:border-[#dce6f4] hover:bg-[#fbfcff]"}`}><div className={`grid size-9 shrink-0 place-items-center rounded-[10px] ${item.color} text-[13px] font-bold shadow-sm`}>{item.icon}</div><div className="min-w-0 flex-1"><div className="flex items-center gap-2"><span className="truncate text-sm font-semibold">{item.name}</span>{item.risky && <ShieldAlert className="size-3.5 text-[#d27d45]" />}</div><p className="mt-0.5 truncate text-[11px] text-[#7c8a9b]">{item.account}</p></div><span className="mr-2 whitespace-nowrap text-[10px] text-[#8491a1]">{item.changed}</span><Star className="size-4 text-[#8b98a8]" /></button>)}</div></section><Detail item={selected} openEditor={openEditor} showPassword={showPassword} setShowPassword={setShowPassword} copy={copy} copied={copied} /></div></section>;
}

function Detail({ item, openEditor, showPassword, setShowPassword, copy, copied }: { item: VaultItem; openEditor: () => void; showPassword: boolean; setShowPassword: (x:boolean)=>void; copy:()=>void; copied:boolean }) { return <section className="relative bg-[#fcfdff] p-5 sm:p-7"><div className="absolute right-6 top-5 flex gap-1"><button onClick={openEditor} className="grid size-8 place-items-center rounded-lg text-[#5f7186] hover:bg-[#eef4ff]"><Pencil className="size-4" /></button><button className="grid size-8 place-items-center rounded-lg text-[#5f7186] hover:bg-[#eef4ff]"><MoreHorizontal className="size-5" /></button></div><div className="mb-7 flex items-center gap-4"><div className={`grid size-12 place-items-center rounded-2xl ${item.color} text-lg font-bold shadow-sm`}>{item.icon}</div><div><div className="flex items-center gap-2"><h2 className="text-lg font-bold">{item.name}</h2><span className="rounded-full bg-[#e8f0ff] px-2 py-0.5 text-[10px] font-medium text-[#3777e7]">{item.tag}</span></div><p className="mt-1 text-xs text-[#7b8b9e]">安全条目 · 已加密保存</p></div></div>{item.risky && <div className="mb-5 flex gap-2 rounded-xl border border-[#f1d9c5] bg-[#fff8f2] p-3 text-xs text-[#9d643c]"><ShieldAlert className="size-4 shrink-0"/>这个密码与其他条目重复，建议立即更新。</div>}<div className="rounded-xl border border-[#e3e9f0] bg-white p-4"><Info label="用户名" value={item.account} copy={copy} copied={copied}/><div className="my-4 border-t border-[#edf0f4]"/><div><p className="mb-2 text-[11px] text-[#7b899a]">密码</p><div className="flex items-center"><span className="flex-1 font-['DM_Mono'] text-sm tracking-[0.16em]">{showPassword ? "Dawn!Harbor_2026" : "••••••••••••••"}</span><button onClick={()=>setShowPassword(!showPassword)} className="mr-2 text-[#6d7e91]">{showPassword ? <EyeOff className="size-4"/> : <Eye className="size-4"/>}</button><button onClick={copy} className="text-[#537292]">{copied ? <Check className="size-4 text-[#34a36a]"/> : <Copy className="size-4"/>}</button></div></div><div className="my-4 border-t border-[#edf0f4]"/><Info label="网址" value={`https://${item.domain}`} external copy={copy} copied={false}/></div><div className="mt-3 rounded-xl border border-[#e4e9ef] bg-[#f9fbfd] p-4 text-[11px] leading-5 text-[#637488]"><div className="flex justify-between"><span>创建时间</span><span>2024/05/10 14:30</span></div><div className="mt-2 flex justify-between"><span>最近使用</span><span>今天 09:15</span></div></div><div className="mt-4 grid grid-cols-2 gap-2"><button onClick={copy} className="flex h-9 items-center justify-center gap-2 rounded-lg border border-[#dce4ee] text-xs font-medium text-[#506174] hover:bg-[#f4f7fb]"><Copy className="size-3.5"/>复制密码</button><button className="flex h-9 items-center justify-center gap-2 rounded-lg bg-[#276cf0] text-xs font-semibold text-white hover:bg-[#1d60df]"><ExternalLink className="size-3.5"/>打开网站</button></div></section>; }
function Info({ label, value, copy, copied, external }: { label:string; value:string; copy:()=>void; copied:boolean; external?:boolean }) { return <div><p className="mb-2 text-[11px] text-[#7b899a]">{label}</p><div className="flex items-center gap-2"><span className={`min-w-0 flex-1 truncate text-xs ${external ? "text-[#2771ed]" : "text-[#334354]"}`}>{value}</span>{external ? <ExternalLink className="size-3.5 text-[#5681bd]"/> : <button onClick={copy}>{copied ? <Check className="size-4 text-[#34a36a]"/> : <Copy className="size-4 text-[#648099]"/>}</button>}</div></div>; }

function Editor({ item, onClose }: { item?: VaultItem; onClose: () => void }) { const [tab, setTab] = useState("基本信息"); const isNew = !item; const draft = item ?? { name: "", account: "", domain: "", tag: "工作", icon: "", color: "", id: 0, changed: "" }; return <div className="absolute inset-y-0 right-0 z-20 flex w-full max-w-[372px] flex-col border-l border-[#dfe6ee] bg-white shadow-[-18px_0_45px_rgba(50,73,105,0.14)]"><header className="flex h-[68px] items-center justify-between border-b border-[#e6ebf1] px-5"><div className="flex items-center gap-2 text-sm font-bold"><ArrowLeft className="size-4"/>{isNew ? "新建密码" : `编辑密码 · ${draft.name}`}</div><button onClick={onClose} className="grid size-8 place-items-center rounded-lg text-[#65778b] hover:bg-[#f1f5fa]"><X className="size-5"/></button></header><div className="flex min-h-0 flex-1"><nav className="w-[98px] shrink-0 bg-[#f5f8fc] px-2 pt-4">{["基本信息","更多信息","高级设置"].map(x=><button key={x} onClick={()=>setTab(x)} className={`mb-1 w-full rounded-lg px-2 py-2 text-left text-[11px] ${tab===x?"bg-[#e4efff] font-semibold text-[#276cf0]":"text-[#708094] hover:bg-[#edf2f8]"}`}>{x}</button>)}</nav><div className="min-w-0 flex-1 overflow-y-auto p-4"><Form label="标题" value={draft.name}/><Form label="用户名" value={draft.account}/><Form label="密码" value="Dawn!Harbor_2026" password/><div className="mb-4"><p className="mb-1.5 text-[11px] text-[#6e7d8e]">安全强度</p><div className="flex gap-1"><i className="h-1 flex-1 rounded bg-[#2771ef]"/><i className="h-1 flex-1 rounded bg-[#2771ef]"/><i className="h-1 flex-1 rounded bg-[#2771ef]"/><i className="h-1 flex-1 rounded bg-[#2771ef]"/></div><span className="mt-1 block text-right text-[10px] text-[#3979e7]">强</span></div><Form label="网址" value={`https://${draft.domain}`}/><label className="mb-4 block text-[11px] text-[#6e7d8e]">分类<select className="mt-1.5 h-9 w-full rounded-lg border border-[#dce4ee] bg-[#fbfcfe] px-2 text-xs text-[#334354]"><option>{draft.tag}</option><option>个人</option></select></label><label className="block text-[11px] text-[#6e7d8e]">备注<textarea className="mt-1.5 h-24 w-full resize-none rounded-lg border border-[#dce4ee] bg-[#fbfcfe] p-2 text-xs text-[#435466]" defaultValue="个人 Google 账号，用于日常登录和同步。"/></label></div></div><footer className="flex gap-2 border-t border-[#e5ebf1] p-4"><button onClick={onClose} className="h-9 flex-1 rounded-lg border border-[#dce4ee] text-xs font-medium text-[#5d6d80]">取消</button><button onClick={onClose} className="h-9 flex-1 rounded-lg bg-[#276cf0] text-xs font-semibold text-white">{isNew ? "创建密码" : "保存更改"}</button></footer></div>; }
function Form({label,value,password}:{label:string;value:string;password?:boolean}) { return <label className="mb-4 block text-[11px] text-[#6e7d8e]">{label}<div className="mt-1.5 flex h-9 items-center rounded-lg border border-[#dce4ee] bg-[#fbfcfe] px-2"><input defaultValue={value} type={password?"password":"text"} className="min-w-0 flex-1 bg-transparent text-xs text-[#334354] outline-none"/>{password&&<Eye className="size-3.5 text-[#73859b]"/>}</div></label>; }

function Preferences({ tab, setTab, onBack }: { tab:string; setTab:(x:string)=>void; onBack:()=>void }) {
  const tabs = ["常规","同步","安全"];
  const titles: Record<string, { title: string; description: string }> = {
    常规: { title: "常规设置", description: "控制应用的日常使用方式" },
    同步: { title: "同步设置", description: "管理加密保险库的同步状态" },
    安全: { title: "安全设置", description: "保护你的保险库与本地数据" },
  };
  const current = titles[tab];
  return <section className="min-w-0 flex-1 bg-[#fbfcfe]"><header className="flex h-[68px] items-center border-b border-[#e4e9ef] px-6 sm:px-8"><button onClick={onBack} title="返回保险库" className="mr-3 grid size-8 place-items-center rounded-lg text-[#5f7186] transition hover:bg-[#eff4fa] hover:text-[#276cf0]"><ArrowLeft className="size-4 stroke-[2]"/></button><h1 className="text-base font-bold">设置</h1></header><div className="mx-auto max-w-[920px] p-6 sm:p-10"><div className="overflow-hidden rounded-2xl border border-[#e1e8f0] bg-white"><div className="flex border-b border-[#e8edf3] px-3 sm:px-5">{tabs.map(x=><button key={x} onClick={()=>setTab(x)} className={`-mb-px border-b-2 px-4 py-3.5 text-sm transition ${tab===x?"border-[#276cf0] font-semibold text-[#276cf0]":"border-transparent text-[#758499] hover:text-[#42617f]"}`}>{x}</button>)}</div><div className="p-5 sm:p-7"><div className="mb-3"><h2 className="text-lg font-bold">{current.title}</h2><p className="mt-1 text-xs text-[#7c8b9d]">{current.description}</p></div>{tab === "常规" && <GeneralSettings/>}{tab === "同步" && <SyncSettings/>}{tab === "安全" && <SecuritySettings/>}</div></div></div></section>;
}
function Toggle({on=true}:{on?:boolean}) { return <button className={`relative h-5 w-9 rounded-full transition ${on?"bg-[#276cf0]":"bg-[#cbd5e1]"}`}><i className={`absolute top-0.5 size-4 rounded-full bg-white shadow transition ${on?"left-4":"left-0.5"}`}/></button>; }
function Setting({icon,title,desc,children}:{icon:React.ReactNode;title:string;desc:string;children:React.ReactNode}) { return <div className="flex items-center gap-3 border-b border-[#edf0f4] py-4 last:border-0"><div className="grid size-9 place-items-center rounded-xl bg-[#edf4ff] text-[#3174e6]">{icon}</div><div className="min-w-0 flex-1"><p className="text-sm font-semibold">{title}</p><p className="mt-0.5 text-xs text-[#79889a]">{desc}</p></div>{children}</div>; }
function GeneralSettings(){return <><Setting icon={<EyeOff className="size-4"/>} title="最小化至托盘" desc="关闭窗口时继续在后台运行"><Toggle/></Setting><Setting icon={<LockKeyhole className="size-4"/>} title="自动锁定" desc="无操作后自动锁定保险库"><select className="rounded-lg border border-[#dce4ee] bg-[#fafcff] px-2 py-1.5 text-xs"><option>5 分钟</option></select></Setting></>}
function SyncSettings(){return <><div className="mb-4 flex justify-end"><span className="rounded-full bg-[#e6f8ed] px-2 py-1 text-[10px] font-semibold text-[#2e9d5b]">已连接</span></div><div className="rounded-xl bg-[#f7faff] p-4 text-xs text-[#627489]"><div className="flex justify-between"><span>同步方式</span><span className="font-medium text-[#33465c]">双向同步</span></div><div className="mt-3 flex justify-between"><span>最后同步</span><span className="font-medium text-[#33465c]">刚刚</span></div></div><div className="mt-5 flex gap-2"><button className="rounded-lg bg-[#276cf0] px-4 py-2 text-xs font-semibold text-white">立即同步</button><button className="rounded-lg border border-[#dce4ee] px-4 py-2 text-xs font-medium text-[#597087]">管理连接</button></div></>}
function SecuritySettings(){return <><div className="mb-2"><h3 className="text-base font-bold">安全</h3><p className="mt-1 text-xs text-[#7c8b9d]">保护你的加密保险库</p></div><Setting icon={<KeyRound className="size-4"/>} title="主密码" desc="定期更换主密码可以加强保护"><button className="rounded-lg border border-[#dce4ee] px-3 py-1.5 text-xs font-medium text-[#51677e]">修改主密码</button></Setting><Setting icon={<ShieldCheck className="size-4"/>} title="Windows Hello" desc="使用生物识别快速解锁"><Toggle/></Setting><Setting icon={<LockKeyhole className="size-4"/>} title="离开时锁定" desc="设备休眠或锁屏时立即锁定"><Toggle/></Setting><button className="mt-5 flex items-center gap-2 text-xs text-[#bd5b57]"><Trash2 className="size-3.5"/>清除本设备的本地数据</button></>}
