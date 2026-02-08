#pragma once

#include <QString>
#include <QList>
#include <QPair>
#include <QMap>

namespace gui {

enum class FriendlyType {
    Slider,
    Dropdown,
    Checkbox
};

struct SettingMeta {
    QString key;
    QString label;
    FriendlyType type;
    int rawMin;
    int rawMax;
    int step;
    QList<QPair<int, QString>> enumLabels;  // value -> display text (for dropdowns)
    bool isCommon;
};

// Returns ordered list of Common settings metadata.
// layoutVersion affects ranges/enums for some keys.
inline QList<SettingMeta> commonSettingsMeta(int layoutVersion) {
    QList<SettingMeta> list;

    // 1. PWM_FREQUENCY
    {
        SettingMeta m;
        m.key = "PWM_FREQUENCY"; m.label = "PWM frequency";
        m.type = FriendlyType::Dropdown; m.rawMin = 0; m.rawMax = 96; m.step = 1;
        m.enumLabels = {{24, "24 kHz"}, {48, "48 kHz"}, {96, "96 kHz"}, {0, "Dynamic"}};
        m.isCommon = true;
        list.append(m);
    }

    // 2. MOTOR_DIRECTION
    {
        SettingMeta m;
        m.key = "MOTOR_DIRECTION"; m.label = "Motor direction";
        m.type = FriendlyType::Dropdown; m.rawMin = 1; m.rawMax = 4; m.step = 1;
        m.enumLabels = {{1, "Normal"}, {2, "Reversed"}};
        m.isCommon = true;
        list.append(m);
    }

    // 3. STARTUP_POWER_MIN
    {
        SettingMeta m;
        m.key = "STARTUP_POWER_MIN"; m.label = "Startup power min";
        m.type = FriendlyType::Slider; m.rawMin = 1000; m.rawMax = 1125; m.step = 5;
        m.isCommon = true;
        list.append(m);
    }

    // 4. STARTUP_POWER_MAX
    {
        SettingMeta m;
        m.key = "STARTUP_POWER_MAX"; m.label = "Startup power max";
        m.type = FriendlyType::Slider; m.rawMin = 1004; m.rawMax = 1300; m.step = 4;
        m.isCommon = true;
        list.append(m);
    }

    // 5. COMMUTATION_TIMING
    {
        SettingMeta m;
        m.key = "COMMUTATION_TIMING"; m.label = "Commutation timing";
        m.type = FriendlyType::Dropdown; m.rawMin = 1; m.rawMax = 5; m.step = 1;
        m.enumLabels = {
            {1, QString::fromUtf8("0\u00b0 (Low)")},
            {2, QString::fromUtf8("7.5\u00b0 (MediumLow)")},
            {3, QString::fromUtf8("15\u00b0 (Medium)")},
            {4, QString::fromUtf8("22.5\u00b0 (MediumHigh)")},
            {5, QString::fromUtf8("30\u00b0 (High)")}
        };
        m.isCommon = true;
        list.append(m);
    }

    // 6. DEMAG_COMPENSATION
    {
        SettingMeta m;
        m.key = "DEMAG_COMPENSATION"; m.label = "Demag compensation";
        m.type = FriendlyType::Dropdown; m.rawMin = 1; m.rawMax = 3; m.step = 1;
        m.enumLabels = {{1, "Off"}, {2, "Low"}, {3, "High"}};
        m.isCommon = true;
        list.append(m);
    }

    // 7. RPM_POWER_SLOPE
    {
        SettingMeta m;
        m.key = "RPM_POWER_SLOPE"; m.label = "RPM power slope";
        m.type = FriendlyType::Dropdown; m.rawMin = 0; m.rawMax = 13; m.step = 1;
        if (layoutVersion >= 201) {
            m.enumLabels = {{0, "Off"}};
            for (int i = 1; i <= 13; ++i) {
                QString lbl = QString::number(i) + "x";
                if (i == 1) lbl += " (More protection)";
                else if (i == 13) lbl += " (Less protection)";
                m.enumLabels.append({i, lbl});
            }
        } else {
            m.enumLabels = {
                {1,  "0.5% (0.031)"},
                {7,  "5% (0.25)"},
                {8,  "7% (0.38)"},
                {9,  "10% (0.50)"},
                {10, "15% (0.75)"},
                {11, "20% (1.00)"},
                {12, "24% (1.25)"},
                {13, "29% (1.50)"}
            };
        }
        m.isCommon = true;
        list.append(m);
    }

    // 8. BEEP_STRENGTH
    {
        SettingMeta m;
        m.key = "BEEP_STRENGTH"; m.label = "Beep strength";
        m.type = FriendlyType::Slider; m.rawMin = 0; m.rawMax = 255; m.step = 1;
        m.isCommon = true;
        list.append(m);
    }

    // 9. BEACON_STRENGTH
    {
        SettingMeta m;
        m.key = "BEACON_STRENGTH"; m.label = "Beacon strength";
        m.type = FriendlyType::Slider; m.rawMin = 0; m.rawMax = 255; m.step = 1;
        m.isCommon = true;
        list.append(m);
    }

    // 10. BEACON_DELAY
    {
        SettingMeta m;
        m.key = "BEACON_DELAY"; m.label = "Beacon delay";
        m.type = FriendlyType::Dropdown; m.rawMin = 1; m.rawMax = 5; m.step = 1;
        m.enumLabels = {
            {1, "1 minute"}, {2, "2 minutes"}, {3, "5 minutes"},
            {4, "10 minutes"}, {5, "Infinite"}
        };
        m.isCommon = true;
        list.append(m);
    }

    // 11. POWER_RATING
    {
        SettingMeta m;
        m.key = "POWER_RATING"; m.label = "Power rating";
        m.type = FriendlyType::Dropdown; m.rawMin = 1; m.rawMax = 2; m.step = 1;
        m.enumLabels = {{1, "1S"}, {2, "2S+"}};
        m.isCommon = true;
        list.append(m);
    }

    // 12. TEMPERATURE_PROTECTION
    {
        SettingMeta m;
        m.key = "TEMPERATURE_PROTECTION"; m.label = "Temperature protection";
        m.type = FriendlyType::Dropdown; m.rawMin = 0; m.rawMax = 7; m.step = 1;
        m.enumLabels = {
            {0, "Disabled"}, {1, "80 C"}, {2, "90 C"}, {3, "100 C"},
            {4, "110 C"}, {5, "120 C"}, {6, "130 C"}, {7, "140 C"}
        };
        m.isCommon = true;
        list.append(m);
    }

    // 13. FORCE_EDT_ARM
    {
        SettingMeta m;
        m.key = "FORCE_EDT_ARM"; m.label = "Force EDT arm";
        m.type = FriendlyType::Checkbox; m.rawMin = 0; m.rawMax = 1; m.step = 1;
        m.isCommon = true;
        list.append(m);
    }

    // 14. BRAKE_ON_STOP
    {
        SettingMeta m;
        m.key = "BRAKE_ON_STOP"; m.label = "Brake on stop";
        m.type = FriendlyType::Checkbox; m.rawMin = 0; m.rawMax = 1; m.step = 1;
        m.isCommon = true;
        list.append(m);
    }

    // 15. BRAKING_STRENGTH
    {
        SettingMeta m;
        m.key = "BRAKING_STRENGTH"; m.label = "Braking strength";
        m.isCommon = true;
        if (layoutVersion == 202) {
            m.type = FriendlyType::Dropdown; m.rawMin = 0; m.rawMax = 2; m.step = 1;
            m.enumLabels = {{0, "Off"}, {1, "Not during startup"}, {2, "On"}};
        } else {
            m.type = FriendlyType::Slider; m.rawMin = 0; m.rawMax = 255; m.step = 1;
        }
        list.append(m);
    }

    // 16. DITHERING
    {
        SettingMeta m;
        m.key = "DITHERING"; m.label = "Dithering";
        m.type = FriendlyType::Checkbox; m.rawMin = 0; m.rawMax = 1; m.step = 1;
        m.isCommon = true;
        list.append(m);
    }

    return list;
}

// Returns ordered list of Advanced settings metadata.
inline QList<SettingMeta> advancedSettingsMeta(int /*layoutVersion*/) {
    QList<SettingMeta> list;

    // LED_CONTROL
    {
        SettingMeta m;
        m.key = "LED_CONTROL"; m.label = "LED control";
        m.type = FriendlyType::Slider; m.rawMin = 0; m.rawMax = 255; m.step = 1;
        m.isCommon = false;
        list.append(m);
    }

    // STARTUP_BEEP
    {
        SettingMeta m;
        m.key = "STARTUP_BEEP"; m.label = "Startup beep";
        m.type = FriendlyType::Checkbox; m.rawMin = 0; m.rawMax = 1; m.step = 1;
        m.isCommon = false;
        list.append(m);
    }

    // THRESHOLD_96TO48
    {
        SettingMeta m;
        m.key = "THRESHOLD_96TO48"; m.label = "Threshold 96\u219248";
        m.type = FriendlyType::Slider; m.rawMin = 0; m.rawMax = 255; m.step = 1;
        m.isCommon = false;
        list.append(m);
    }

    // THRESHOLD_48TO24
    {
        SettingMeta m;
        m.key = "THRESHOLD_48TO24"; m.label = "Threshold 48\u219224";
        m.type = FriendlyType::Slider; m.rawMin = 0; m.rawMax = 255; m.step = 1;
        m.isCommon = false;
        list.append(m);
    }

    return list;
}

}
