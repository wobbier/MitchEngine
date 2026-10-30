#pragma once
#include "ScriptNaming.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// Create > C# Script's templates (ME-237). Each new script goes in a namespace named after the
// project (file-scoped), so two projects' or mods' scripts can't clash by namespace; the engine
// still finds script classes by their class name, so those stay unique across the project.
namespace ScriptTemplates
{
    struct Template
    {
        std::string Label;      // the menu entry
        std::string BaseName;   // the new file and class name, before it's made unique
        std::string Tooltip;
        std::string Body;       // the class, "$NAME" for its name
    };


    inline const std::vector<Template>& All()
    {
        static const std::vector<Template> kTemplates = {
            { "Script", "NewScript", "A component script: OnStart and OnUpdate on an entity.",
                "public class $NAME : Script\n"
                "{\n"
                "    public override void OnStart()\n"
                "    {\n"
                "    }\n\n"
                "    public override void OnUpdate(float dt)\n"
                "    {\n"
                "    }\n"
                "}\n" },
            { "Networked Script", "NewNetworkedScript", "A script with replicated state and an RPC (partial: the NetGenerator fills in the rest).",
                "public partial class $NAME : Script\n"
                "{\n"
                "    // Server-authoritative: sent to clients in snapshots. [Predicted] fields are also\n"
                "    // predicted by the owning client and resimulated on corrections.\n"
                "    [Replicated] public int Score;\n\n"
                "    // Called on the owning client; runs on the server as OnRequestScore.\n"
                "    [ServerRpc] partial void RequestScore(int points);\n\n"
                "    void OnRequestScore(int points)\n"
                "    {\n"
                "        Score += points;\n"
                "    }\n\n"
                "    public override void OnStart()\n"
                "    {\n"
                "    }\n\n"
                "    public override void OnUpdate(float dt)\n"
                "    {\n"
                "    }\n"
                "}\n" },
            { "UI Controller", "NewUIController", "Drives an RmlUi document's data-model: sets values, handles its events.",
                "public class $NAME : Script\n"
                "{\n"
                "    // The data-model=\"...\" name in the .rml document this controls.\n"
                "    public string Model = \"hud\";\n\n"
                "    public override void OnStart()\n"
                "    {\n"
                "        var model = UI.Model(Model);\n"
                "        model.Set(\"title\", \"Hello\");\n"
                "        model.On(\"clicked\");     // data-event-click=\"clicked\" in the document\n"
                "    }\n\n"
                "    public override void OnUpdate(float dt)\n"
                "    {\n"
                "        var model = UI.Model(Model);\n"
                "        for (string e; (e = model.PollEvent()) != \"\";)\n"
                "        {\n"
                "            if (e == \"clicked\")\n"
                "            {\n"
                "                Debug.Log(\"clicked\");\n"
                "            }\n"
                "        }\n"
                "    }\n"
                "}\n" },
            { "Data Asset", "NewDataAsset", "Tuning data in .asset files (Create > Data Asset once it builds), loaded with AssetRef.Load<T>().",
                "public class $NAME : DataAsset\n"
                "{\n"
                "    public float Value = 1f;\n\n"
                "    // After the values load, and again when the .asset changes while playing.\n"
                "    public override void OnLoaded()\n"
                "    {\n"
                "    }\n"
                "}\n" },
            { "Class", "NewClass", "A plain C# class (not a script).",
                "public class $NAME\n"
                "{\n"
                "}\n" },
        };
        return kTemplates;
    }


    inline const Template* Find( const std::string& InLabel )
    {
        for( const Template& entry : All() )
        {
            if( entry.Label == InLabel )
            {
                return &entry;
            }
        }
        return nullptr;
    }


    // The project's namespace: its name as an identifier ("My Game" -> "MyGame"), "Game" when that's empty.
    inline std::string ProjectNamespace( const std::string& InProjectName )
    {
        const bool hasName = std::any_of( InProjectName.begin(), InProjectName.end(), []( char c ) { return std::isalnum( static_cast<unsigned char>( c ) ) != 0; } );
        const std::string name = hasName ? ScriptNaming::MakeIdentifier( InProjectName ) : std::string();
        return name.empty() || name == "ScriptCore" || name == "System" ? "Game" : name;
    }


    inline std::string Make( const Template& InTemplate, const std::string& InClassName, const std::string& InNamespace )
    {
        std::string body = InTemplate.Body;
        for( size_t at = body.find( "$NAME" ); at != std::string::npos; at = body.find( "$NAME", at + InClassName.size() ) )
        {
            body.replace( at, 5, InClassName );
        }
        std::string source = "using System;\nusing ScriptCore;\n\n";
        if( !InNamespace.empty() )
        {
            source += "namespace " + InNamespace + ";\n\n";
        }
        return source + body;
    }
}
