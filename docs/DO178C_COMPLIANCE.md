# DO-178C Uyumluluk Raporu

## Proje: Communication Library
## Tarih: 2025-11-25
## DAL Seviyesi: B (Hazardous/Severe-Major)

---

## 1. DO-178C Gereksinimleri ve Uyumluluk Durumu

### Tablo A-1: Yazılım Planlama Süreci
| Gereksinim | Durum | Açıklama |
|------------|-------|----------|
| Plan-01: Yazılım geliştirme planı | ✅ | Bu doküman |
| Plan-02: Yazılım doğrulama planı | ✅ | Test stratejisi tanımlı |
| Plan-03: Yapılandırma yönetimi planı | ✅ | Git ile versiyon kontrolü |

### Tablo A-2: Yazılım Geliştirme Süreci
| Gereksinim | Durum | Açıklama |
|------------|-------|----------|
| Dev-01: Gereksinim izlenebilirliği | ✅ | COMM_REQUIREMENT makrosu |
| Dev-02: Tasarım standartları | ✅ | C++17, MISRA uyumlu |
| Dev-03: Kod standartları | ✅ | clang-format, static analysis |

### Tablo A-3: Doğrulama Çıktıları
| Gereksinim | Durum | Açıklama |
|------------|-------|----------|
| Ver-01: Gereksinim doğrulama | ✅ | Birim testleri |
| Ver-02: Tasarım doğrulama | ✅ | Entegrasyon testleri |
| Ver-03: Kod doğrulama | ✅ | Statik analiz |

---

## 2. Kod Güvenlik Özellikleri

### 2.1 Dinamik Bellek Yönetimi
```
❌ std::vector<>.push_back() - Kritik yollarda KULLANILMAZ
✅ do178c::FixedBuffer<T, N> - Sabit boyutlu buffer kullanılır
```

**Uygulama:**
```cpp
// YANLIŞ - Dinamik allocation
std::vector<uint8_t> buffer;
buffer.push_back(data);  // Heap allocation!

// DOĞRU - Sabit boyutlu buffer
do178c::FixedBuffer<uint8_t, 64> buffer;
buffer.push_back(data);  // Stack allocation, bounded
```

### 2.2 Sınırlı Döngüler (Bounded Loops)
```
❌ while(true) - YASAK
❌ for(;;) - YASAK
✅ COMM_BOUNDED_FOR(i, 0, n, max) - Zorunlu
✅ COMM_BOUNDED_WHILE(cond, max) - Zorunlu
```

**Uygulama:**
```cpp
// YANLIŞ - Sınırsız döngü
while (running) {
    process();
}

// DOĞRU - Sınırlı döngü
COMM_BOUNDED_WHILE(running, kMaxIterations) {
    process();
}
```

### 2.3 Assertion Framework
```cpp
// Önkoşul kontrolü
COMM_PRECONDITION(ptr != nullptr);

// Sonkoşul kontrolü  
COMM_POSTCONDITION(result >= 0);

// Değişmez kontrolü
COMM_INVARIANT(size <= capacity);

// Her zaman aktif assertion (release'de de çalışır)
COMM_ASSERT_ALWAYS(critical_condition, "Critical failure");
```

### 2.4 Güvenli Aritmetik
```cpp
// YANLIŞ - Taşma riski
uint64_t result = a + b;

// DOĞRU - Taşma kontrolü
uint64_t result;
if (!do178c::safeAdd(a, b, result)) {
    handleOverflow();
}
```

### 2.5 İzlenebilirlik Annotasyonları
```cpp
COMM_REQUIREMENT("SRS-COMM-CAN-001")  // Gereksinim bağlantısı
COMM_TESTCASE("TC-COMM-001")           // Test case bağlantısı
COMM_BRANCH("send-success")            // Dal kaplama izleme
COMM_DECISION("send-status-check")     // Karar kaplama izleme
COMM_SAFETY_CRITICAL                   // Güvenlik kritik işaretleyici
```

---

## 3. Güvenlik Mekanizmaları

### 3.1 Watchdog Monitoring
```cpp
// Başlatma
safety::Watchdog watchdog(1000ms);

// Periyodik besleme
watchdog.feed();

// Timeout kontrolü
if (watchdog.check() == Watchdog::State::Timeout) {
    enterFailsafeMode();
}
```

### 3.2 Failsafe State Machine
```
Normal → Degraded → FailSafe → Emergency
   ↑         ↑          ↑          ↑
   └─────────┴──────────┴──────────┘
              (Recovery)
```

### 3.3 Sequence Validation
```cpp
// Gönderim
uint32_t seq = sequenceGen.next();
frame.sequence = seq;

// Alım
auto result = sequenceValidator.validate(frame.sequence);
switch (result) {
    case Result::Valid:      // Normal
    case Result::Gap:        // Mesaj kaybı!
    case Result::Duplicate:  // Tekrar eden mesaj!
}
```

### 3.4 CRC32 Checksum
```cpp
// Hesaplama
uint32_t crc = calculateCrc32(data, length);

// Doğrulama
if (!verifyCrc32(data, length)) {
    reportIntegrityError();
}
```

---

## 4. MC/DC Kaplama Gereksinimleri

DO-178C Level B için Modified Condition/Decision Coverage (MC/DC) gereklidir.

### 4.1 Dal (Branch) İşaretleme
```cpp
COMM_DECISION("send-status-check");
if (status != PCAN_ERROR_OK) {
    COMM_BRANCH("send-failed");
    // ...
} else {
    COMM_BRANCH("send-success");
    // ...
}
```

### 4.2 Kaplama Metrikleri
| Metrik | Hedef | Açıklama |
|--------|-------|----------|
| Statement Coverage | 100% | Her satır en az 1 kez |
| Branch Coverage | 100% | Her dal en az 1 kez |
| MC/DC | 100% | Her koşul bağımsız test |

---

## 5. Statik Analiz Gereksinimleri

### 5.1 MISRA C++ 2008 Uyumluluğu
- Rule 0-1-1: Unreachable code ❌
- Rule 5-0-3: No implicit integral conversions ✅
- Rule 6-4-1: Switch shall have default ✅
- Rule 15-0-1: No exceptions in critical code ✅

### 5.2 Kullanılan Araçlar
- Clang-Tidy
- CppCheck
- PC-lint Plus
- Polyspace

---

## 6. Test Stratejisi

### 6.1 Birim Testleri
```cpp
TEST(PCANBasic, SendValidData) {
    // COMM_TESTCASE("TC-COMM-CAN-SEND-001")
    PCANBasic can(config);
    can.open();
    
    ByteVector data = {0x01, 0x02, 0x03};
    auto result = can.send(data);
    
    EXPECT_EQ(result, 3);
}
```

### 6.2 Robustness Testleri
- Null pointer girdisi
- Sıfır boyutlu buffer
- Maksimum boyut aşımı
- Geçersiz handle değerleri

### 6.3 Stres Testleri
- Yüksek mesaj yükü
- Uzun süreli çalışma
- Kaynak tükenmesi senaryoları

---

## 7. Versiyon Geçmişi

| Versiyon | Tarih | Değişiklikler | Onay |
|----------|-------|---------------|------|
| 1.0.0 | 2025-11-01 | İlk sürüm | - |
| 2.0.0 | 2025-11-25 | DO-178C uyumluluk | - |

---

## 8. Onay

| Rol | İsim | Tarih | İmza |
|-----|------|-------|------|
| Geliştirici | | | |
| Doğrulayıcı | | | |
| Kalite Güvence | | | |
| Sertifikasyon | | | |

---

## Ekler

### Ek A: Gereksinim İzlenebilirlik Matrisi
| Gereksinim ID | Kaynak | Kod Konumu | Test Case |
|---------------|--------|------------|-----------|
| SRS-COMM-CAN-001 | SRS v1.0 | pcan_basic.cpp:30 | TC-001 |
| SRS-COMM-CAN-010 | SRS v1.0 | pcan_basic.cpp:120 | TC-010 |
| SRS-SAFETY-001 | SRS v1.0 | pcan_basic.cpp:450 | TC-S01 |

### Ek B: Kullanılan Standartlar
- DO-178C: Software Considerations in Airborne Systems
- DO-254: Design Assurance Guidance for Airborne Electronic Hardware
- MISRA C++ 2008: Guidelines for C++ in Critical Systems
- ISO 26262: Functional Safety for Road Vehicles

### Ek C: Sözlük
| Terim | Tanım |
|-------|-------|
| DAL | Design Assurance Level |
| MC/DC | Modified Condition/Decision Coverage |
| PCAN | Peak CAN Interface |
| CRC | Cyclic Redundancy Check |
