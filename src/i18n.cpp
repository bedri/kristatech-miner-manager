#include "i18n.h"

#include <QSettings>

I18n& I18n::instance()
{
    static I18n s_instance;
    return s_instance;
}

I18n::I18n(QObject* parent)
    : QObject(parent)
{
    initTable();
    QSettings settings(QStringLiteral("KristaTech"), QStringLiteral("MinerManager"));
    const QString saved = settings.value(QStringLiteral("ui/language"), QStringLiteral("tr")).toString();
    m_language = (saved == QStringLiteral("en")) ? Language::EN : Language::TR;
}

void I18n::setLanguage(Language lang)
{
    if (m_language == lang) {
        return;
    }
    m_language = lang;
    QSettings settings(QStringLiteral("KristaTech"), QStringLiteral("MinerManager"));
    settings.setValue(QStringLiteral("ui/language"), languageCode());
    Q_EMIT languageChanged(m_language);
}

void I18n::toggleLanguage()
{
    setLanguage(m_language == Language::EN ? Language::TR : Language::EN);
}

QString I18n::translate(const QString& key) const
{
    auto it = m_table.constFind(key);
    if (it == m_table.constEnd()) {
        return key;
    }
    return (m_language == Language::TR) ? it->second : it->first;
}

void I18n::initTable()
{
    auto add = [this](const char* key, const char* en, const char* tr) {
        m_table.insert(QString::fromUtf8(key), {QString::fromUtf8(en), QString::fromUtf8(tr)});
    };

    // App & Header
    add("app.title", "KristaTech Miner Manager", "KristaTech Madenci Yöneticisi");
    add("app.subtitle", "External cpuminer & ccminer Fleet Orchestrator (ADAM Consensus)",
        "Harici cpuminer & ccminer Filo Yöneticisi (ADAM Konsensüsü)");

    // Toolbar buttons
    add("btn.add_worker", "+ Add Worker", "+ İşçi Ekle");
    add("btn.discover", "Auto-Discover Fleet", "Filoyu Otomatik Keşfet");
    add("btn.start_all", "Start All", "Tümünü Başlat");
    add("btn.stop_all", "Stop All", "Tümünü Durdur");
    add("btn.stop_unreg", "Stop Unregistered", "Kayıtsızları Durdur");
    add("btn.disable_gen", "Disable Internal Gen", "Dahili Kazımı Kapat");
    add("btn.enable_gen", "Enable Internal Gen", "Dahili Kazımı Aç");
    add("btn.import", "Import", "İçe Aktar");
    add("btn.export", "Export", "Dışa Aktar");
    add("btn.lang", "EN / TR", "TR / EN");
    add("btn.theme_dark", "Light Mode", "Açık Tema");
    add("btn.theme_light", "Dark Mode", "Koyu Tema");

    // KPI Cards
    add("kpi.workers", "ACTIVE WORKERS", "AKTİF İŞÇİLER");
    add("kpi.pool_seats", "ADAM POOL SEATS", "ADAM HAVUZ KOLTUKLARI");
    add("kpi.hashrate", "TOTAL HASHRATE", "TOPLAM HASH HIZI");
    add("kpi.solutions", "SOLUTIONS (A / S / R)", "ÇÖZÜMLER (K / B / R)");
    add("kpi.gpu", "CUDA GPU TELEMETRY", "CUDA GPU TELEMETRİ");
    add("kpi.network", "NETWORK & INTERNAL GEN", "AĞ & DAHİLİ KAZIM");
    add("kpi.no_gpu", "No NVIDIA GPU / nvidia-smi", "NVIDIA GPU yok / nvidia-smi bulunamadı");
    add("kpi.double_mine_warn", "Internal CPU Gen ON", "Dahili CPU Kazımı AÇIK");
    add("kpi.double_mine_clean", "Internal CPU Gen OFF", "Dahili CPU Kazımı KAPALI");

    // Filters
    add("filter.all", "All", "Tümü");
    add("filter.running", "Running", "Çalışan");
    add("filter.stopped", "Stopped", "Durmuş");
    add("filter.warnings", "Warnings / Waste", "Uyarı / Boşa Çalışan");
    add("filter.engine_all", "All Engines", "Tüm Motorlar");
    add("filter.target_all", "All Targets", "Tüm Hedefler");
    add("filter.search_ph", "Search worker, address, pod, host...", "İşçi, adres, pod veya sunucu ara...");
    add("opt.auto_stop_deferred", "Auto-Stop Deferred", "Erteleneni Oto-Durdur");
    add("opt.auto_stop_deferred_tip",
        "Automatically stop a running worker when ADAM Auto-Register deferred is detected in its log.",
        "Logda ADAM Auto-Register deferred tespit edildiğinde çalışan işçiyi otomatik olarak durdurur.");

    // Table Columns
    add("col.worker", "Worker", "İşçi");
    add("col.engine", "Engine & HW", "Motor & Donanım");
    add("col.target", "Target Node", "Hedef Düğüm");
    add("col.address", "Coinbase Address", "Coinbase Adresi");
    add("col.pool", "ADAM Pool Status", "ADAM Havuz Durumu");
    add("col.state", "Runtime State", "Çalışma Durumu");
    add("col.perf", "Hashrate / A·S·R", "Hashrate / K·B·R");
    add("col.actions", "Actions", "İşlemler");

    // Status labels
    add("status.stopped", "Stopped", "Durdu");
    add("status.starting", "Starting...", "Başlatılıyor...");
    add("status.waiting", "Waiting Election", "Seçim Bekliyor");
    add("status.solving", "Solving", "Çözüyor");
    add("status.adopted", "Adopted", "Sahiplenildi");
    add("status.error", "Error", "Hata");
    add("status.restarting", "Restarting...", "Yeniden Başlatılıyor...");

    // Pool badges & warnings
    add("pool.registered", "Registered", "Kayıtlı");
    add("pool.unregistered", "Unregistered (Waste Risk)", "Kayıtsız (Boşa Çalışma Riski)");
    add("pool.deferred", "Auto-Register Deferred", "Otomatik Kayıt Ertelendi");
    add("pool.checking", "Checking RPC...", "RPC Kontrol Ediliyor...");
    add("pool.rpc_offline", "RPC Unreachable", "RPC Erişilemiyor");
    add("pool.double_mining", "Double Mining Active", "Çift Madencilik Aktif");
    add("warn.waste_banner",
        "Warning: Selected worker address is NOT registered or Auto-Register is deferred. It cannot earn extra ADAM seats.",
        "Uyarı: Seçili işçi adresi havuza kayıtlı değil veya otomatik kayıt ertelendi. Ek ADAM koltuğu kazanamaz.");
    add("warn.double_banner",
        "Notice: Target node also has internal CPU mining (generate=true) enabled alongside this external worker.",
        "Bilgi: Hedef düğümde harici işçi yanında dahili CPU madenciliği (generate=true) de açık.");
    add("btn.fix_disable_gen", "Disable Internal Gen on Target", "Hedefte Dahili Kazımı Kapat");

    // Row actions & Inspector
    add("act.start", "Start", "Başlat");
    add("act.stop", "Stop", "Durdur");
    add("act.restart", "Restart", "Yeniden Başlat");
    add("act.edit", "Edit", "Düzenle");
    add("act.delete", "Delete", "Sil");
    add("log.header", "Worker Inspector & Live Output", "İşçi Denetçisi & Canlı Çıktı");
    add("log.select_hint", "Select a worker row above to inspect live logs and telemetry.",
        "Canlı logları ve telemetriyi görmek için yukarıdan bir işçi satırı seçin.");
    add("log.copy_addr", "Copy Address", "Adresi Kopyala");
    add("log.clear", "Clear Log", "Logu Temizle");
    add("log.autoscroll", "Auto-Scroll", "Otomatik Kaydır");

    // Targets & Engines
    add("target.local", "Local Wallet", "Yerel Cüzdan");
    add("target.podman", "Podman Pod", "Podman Konteyneri");
    add("target.custom", "Custom RPC", "Özel RPC");
    add("target.ssh", "Remote SSH", "Uzak SSH");

    // Worker Dialog
    add("dlg.add_title", "Add External Miner Worker", "Harici Madenci İşçisi Ekle");
    add("dlg.edit_title", "Edit External Miner Worker", "Harici Madenci İşçisini Düzenle");
    add("dlg.name", "Worker Name:", "İşçi Adı:");
    add("dlg.id", "Worker ID:", "İşçi Kimliği:");
    add("dlg.engine", "Miner Engine:", "Madenci Motoru:");
    add("dlg.binary", "Binary Path:", "İkili Dosya Yolu:");
    add("dlg.browse", "Browse...", "Gözat...");
    add("dlg.test_bin", "Test Version", "Sürümü Test Et");
    add("dlg.target_type", "Target Node Type:", "Hedef Düğüm Türü:");
    add("dlg.conf_path", "kristatech.conf Path:", "kristatech.conf Yolu:");
    add("dlg.podman_name", "Podman Container:", "Podman Konteyneri:");
    add("dlg.rpc_url", "RPC URL:", "RPC Adresi (URL):");
    add("dlg.rpc_user", "RPC User:", "RPC Kullanıcısı:");
    add("dlg.rpc_pass", "RPC Password:", "RPC Parolası:");
    add("dlg.ssh_target", "SSH User@Host:", "SSH Kullanıcı@Sunucu:");
    add("dlg.ssh_port", "SSH Port:", "SSH Portu:");
    add("dlg.remote_bin", "Remote Binary Path:", "Uzak İkili Yolu:");
    add("dlg.address", "Coinbase Address:", "Coinbase Adresi:");
    add("dlg.threads", "CPU Threads (-t):", "CPU İş Parçacığı (-t):");
    add("dlg.affinity", "CPU Core Pin (taskset -c):", "CPU Çekirdek Sabitleme (taskset -c):");
    add("dlg.max_temp", "Max CPU Temp (°C, 0=off):", "Maks. CPU Sıcaklığı (°C, 0=kapalı):");
    add("dlg.gpu_devices", "GPU Devices (-d):", "GPU Cihazları (-d):");
    add("dlg.gpu_intensity", "GPU Intensity (-i, 0=auto):", "GPU Yoğunluğu (-i, 0=otomatik):");
    add("dlg.extra_args", "Extra CLI Flags:", "Ek CLI Parametreleri:");
    add("dlg.auto_restart", "Auto-restart on unexpected exit (backoff)", "Beklenmedik kapanmada otomatik yeniden başlat");
    add("dlg.disable_gen_start", "Disable node internal CPU mining (setgenerate false) on start",
        "Başlarken hedefteki dahili CPU madenciliğini kapat (setgenerate false)");
    add("dlg.save", "Save Worker", "İşçiyi Kaydet");
    add("dlg.cancel", "Cancel", "İptal");
    add("dlg.unreg_warn",
        "Note: This address is not currently in the active ADAM pool. You can still save and run it, or pick a registered seat.",
        "Not: Bu adres şu anda aktif ADAM havuzunda değil. Yine de kaydedip çalıştırabilir veya kayıtlı bir koltuk seçebilirsiniz.");

    // Tray & Close dialogs
    add("tray.show", "Show Miner Manager", "Yöneticiyi Göster");
    add("tray.quit", "Quit Application", "Uygulamadan Çık");
    add("tray.minimized_title", "KristaTech Miner Manager", "KristaTech Madenci Yöneticisi");
    add("tray.minimized_msg", "Minimized to system tray. Miners continue running.",
        "Sistem tepsisine küçültüldü. Madenciler çalışmaya devam ediyor.");
    add("quit.title", "Active Miners Running", "Aktif Madenciler Çalışıyor");
    add("quit.msg",
        "There are %1 running local/Podman miner worker(s).\n\nWhat should happen to managed processes before exiting?\n(Remote SSH miners always stay running on remote hosts unless stopped explicitly.)",
        "%1 adet yerel/Podman madenci işçisi çalışıyor.\n\nÇıkmadan önce yönetilen süreçlere ne yapılsın?\n(Uzak SSH madencileri açıkça durdurulmadıkça uzak sunucuda çalışmaya devam eder.)");
    add("quit.stop_and_exit", "Stop Managed Miners && Quit", "Madencileri Durdur ve Çık");
    add("quit.detach_and_exit", "Detach (Leave Running) && Quit", "Arka Planda Bırak (Detach) ve Çık");
    add("quit.cancel", "Cancel", "İptal");

    add("confirm.delete_title", "Delete Worker Profile", "İşçi Profilini Sil");
    add("confirm.delete_msg", "Stop (if running locally) and delete profile \"%1\"?",
        "\"%1\" profili durdurulup (yerelse) listeden silinsin mi?");
    add("confirm.stop_unreg_title", "Stop Unregistered Workers", "Kayıtsız İşçileri Durdur");
    add("confirm.stop_unreg_msg", "Stop %1 running unregistered/deferred worker(s) to stop wasted CPU/GPU cycles?",
        "Boşa CPU/GPU harcamasını durdurmak için %1 kayıtsız/ertelenmiş işçi durdurulsun mu?");
    add("msg.discover_done", "Fleet discovery complete: %1 new worker(s) added, %2 running process(es) adopted.",
        "Filo keşfi tamamlandı: %1 yeni işçi eklendi, %2 çalışan süreç sahiplenildi.");
    add("msg.export_ok", "Profiles exported cleanly to %1 (local/Podman passwords omitted).",
        "Profiller %1 dosyasına aktarıldı (yerel/Podman parolaları hariç tutuldu).");
    add("msg.import_ok", "Imported %1 worker profile(s) from %2.",
        "%2 dosyasından %1 işçi profili içe aktarıldı.");
    add("msg.gen_disabled", "Disabled internal CPU mining (setgenerate false) on %1.",
        "%1 üzerinde dahili CPU madenciliği kapatıldı (setgenerate false).");
    add("msg.gen_enabled", "Enabled internal CPU mining (setgenerate true) on %1.",
        "%1 üzerinde dahili CPU madenciliği açıldı (setgenerate true).");
}
