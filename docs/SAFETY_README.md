# Flight-Critical Safety Module

## Genel Bakış

Bu modül, DO-178C ve DO-254 standartlarına uygun uçuş kritik aviyonik sistemler için tasarlanmış güvenlik mekanizmalarını sağlar.

## Güvenlik Özellikleri

### 1. CRC32 Checksum (Veri Bütünlüğü)
```cpp
#include <comm/core/safety.hpp>

// Veri gönderirken CRC hesapla
std::vector<uint8_t> data = {0x01, 0x02, 0x03};
uint32_t crc = comm::safety::calculateCrc32(data.data(), data.size());

// Veri alırken CRC doğrula
bool valid = comm::safety::verifyCrc32(receivedData.data(), receivedData.size());
```

### 2. Sequence Numbering (Mesaj Sıralama)
```cpp
comm::safety::SequenceGenerator seqGen;
comm::safety::SequenceValidator seqValidator;

// Gönderirken
uint32_t seq = seqGen.next();
frame.sequence = seq;

// Alırken
auto result = seqValidator.validate(frame.sequence);
if (result == SequenceValidator::Result::Gap) {
    // Mesaj kaybı tespit edildi!
}
```

### 3. Watchdog / Heartbeat (Canlılık Kontrolü)
```cpp
comm::safety::Watchdog watchdog(std::chrono::milliseconds{500});

// Her başarılı iletişimde
watchdog.feed();

// Periyodik kontrol
auto state = watchdog.check();
if (state == Watchdog::State::Timeout) {
    // İletişim koptu - failsafe moduna geç!
}
```

### 4. Failsafe State Machine (Güvenli Duruş)
```cpp
comm::safety::FailsafeManager failsafe;

// Hata raporla
failsafe.reportFault();

// Durum kontrol
auto state = failsafe.getState();
switch (state) {
    case FailsafeState::Normal:    // Normal çalışma
    case FailsafeState::Degraded:  // Azaltılmış işlevsellik
    case FailsafeState::FailSafe:  // Güvenli mod
    case FailsafeState::Emergency: // Acil durum
}
```

### 5. Redundancy Manager (Yedekli Kanal)
```cpp
comm::safety::RedundancyManager<2> redundancy;  // İki kanal

// Kanal durumu güncelle
redundancy.updateChannelHealth(0, ChannelHealth::Healthy);
redundancy.updateChannelHealth(1, ChannelHealth::Failed);

// En iyi kanal seç
auto channel = redundancy.getPrimaryChannel();
if (!channel.has_value()) {
    // Tüm kanallar çökmüş - acil durum!
}
```

### 6. Sanity Checks (Girdi Doğrulama)
```cpp
// Mesaj boyutu kontrolü
if (!comm::safety::isValidMessageSize(size)) {
    return Error::InvalidSize;
}

// CAN ID kontrolü
if (!comm::safety::isValidCanId(canId, extended)) {
    return Error::InvalidCanId;
}

// Port kontrolü
if (!comm::safety::isValidPort(port)) {
    return Error::InvalidPort;
}
```

### 7. Safe Frame Header (Güvenli Çerçeve)
```cpp
comm::safety::SafeFrameHeader header;
header.magic = comm::safety::kFrameMagic;
header.sequence = seqGen.next();
header.timestamp = getTimestamp();
header.payloadLength = payload.size();

// Doğrulama
if (!comm::safety::isValidFrameHeader(header)) {
    return Error::InvalidFrame;
}
```

## Transport Entegrasyonu

SafetyMixin sınıfı tüm transport'lara güvenlik özelliklerini ekler:

```cpp
#include <comm/core/safety_mixin.hpp>

class MyTransport : public ICommunication, protected SafetyMixin {
public:
    ptrdiff_t send(const ByteVector& data) override {
        // ... gönderim işlemi ...
        
        if (success) {
            recordSuccess();  // Watchdog beslenir, hatalar sıfırlanır
        } else {
            recordFailure();  // Ardışık hata sayısı artar
        }
    }
    
    // Safety API'leri otomatik olarak miras alınır:
    // - feedWatchdog()
    // - checkWatchdog()
    // - getFailsafeState()
    // - getNextSequence()
    // - validateSequence()
    // - getSafetyStats()
};
```

## Aviyonik Sistemlerde Kullanım Önerileri

### DO-178C Uyumluluk Kontrol Listesi

- [x] **Deterministic Timing**: `std::chrono::steady_clock` kullanılıyor
- [x] **No Dynamic Allocation in Hot Path**: Tüm buffer'lar önceden tahsis edilmeli
- [x] **No Exceptions**: Tüm fonksiyonlar `noexcept` işaretli
- [x] **Bounded Execution**: Tüm döngüler sınırlı iterasyonlu
- [x] **Input Validation**: Her girdi kontrol ediliyor
- [x] **Error Propagation**: Her hata raporlanıyor ve izleniyor
- [x] **Graceful Degradation**: Kademeli failsafe durumları

### Kritik Yapılandırma

```cpp
// Watchdog timeout - sistem gereksinimlerine göre ayarlayın
constexpr auto kHeartbeatTimeout = std::chrono::milliseconds{100};

// Maksimum ardışık hata sayısı
constexpr size_t kMaxConsecutiveErrors = 3;

// CRC kontrolü her zaman aktif olmalı
constexpr bool kEnableCrcVerification = true;

// Sequence kontrolü her zaman aktif olmalı  
constexpr bool kEnableSequenceValidation = true;
```

### Örnek: Uçuş Kontrol CAN Haberleşmesi

```cpp
#include <comm/can/pcan_basic.hpp>
#include <comm/core/safety.hpp>

class FlightControlCAN {
public:
    void sendCommand(const FlightCommand& cmd) {
        // 1. Sequence number ekle
        auto seq = transport_.getNextSequence();
        
        // 2. Veriyi hazırla
        ByteVector payload = serialize(cmd);
        
        // 3. CRC ekle
        payload.resize(payload.size() + 4);
        safety::appendCrc32(payload.data(), payload.size() - 4);
        
        // 4. Gönder
        auto result = transport_.send(payload);
        
        // 5. Watchdog kontrolü
        if (transport_.checkWatchdog() == Watchdog::State::Timeout) {
            handleCommunicationLoss();
        }
    }
    
    void receiveResponse(ByteVector& response) {
        auto result = transport_.receive(response, 64);
        
        if (result > 0) {
            // 1. CRC doğrula
            if (!safety::verifyCrc32(response.data(), response.size())) {
                handleCrcError();
                return;
            }
            
            // 2. Sequence doğrula
            auto seq = extractSequence(response);
            if (transport_.validateSequence(seq) == SequenceValidator::Result::Gap) {
                handleMessageLoss();
            }
        }
        
        // 3. Failsafe durumu kontrol
        if (transport_.getFailsafeState() != FailsafeState::Normal) {
            enterSafeMode();
        }
    }
    
private:
    PCANBasic transport_;
};
```

## Test Stratejisi

### Birim Testleri
- CRC hesaplama doğruluğu
- Sequence wrap-around davranışı
- Watchdog timeout tespiti
- Failsafe state geçişleri

### Entegrasyon Testleri
- Kablo koparılması simülasyonu
- Gürültülü kanal simülasyonu
- Yüksek yük altında davranış
- Çift kanal yedekleme

### Donanım-in-the-Loop (HIL)
- Gerçek aviyonik donanım ile test
- Elektromanyetik girişim testi
- Sıcaklık ve vibrasyon testleri

## Sürüm Geçmişi

| Sürüm | Tarih | Değişiklikler |
|-------|-------|---------------|
| 1.0.0 | 2025-11-25 | İlk sürüm - CRC, Sequence, Watchdog, Failsafe |

## İletişim

Bu modül ile ilgili sorularınız için aviyonik ekibiyle iletişime geçin.
