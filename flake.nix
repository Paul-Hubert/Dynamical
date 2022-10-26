{
  description = "Projet programmation graphique";
    
  inputs = {
    system-config.url = "path:/etc/nixos/";
    flake-utils = {
      url = "github:numtide/flake-utils";
    };
  };
  
  outputs = { self, system-config, flake-utils }: flake-utils.lib.eachDefaultSystem (system:
    let
      pkgs = system-config.outputs.pkgs.${system}.nixpkgs; 
    in rec {
      devShell = defaultPackage;
      defaultPackage = pkgs.stdenv.mkDerivation rec {
        pname = "dynamical";
        version = "0.1.0";
        src = ./.;

        buildInputs = with pkgs; [ 
          cmake 
          clang 
          ninja 
          vulkan-tools
          vulkan-headers
          vulkan-loader
          vulkan-validation-layers  
          SDL2
          imgui
          cereal
          glm
          taskflow
          bullet
          nlohmann_json
          stb
          glslang

          # Perso
          libclang
          lldb
          vcpp
        ];
        
        shellHook = ''
          export PATH="${pkgs.lib.makeBinPath buildInputs}:$PATH"
          export VK_LAYER_PATH="${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d"
        '';
      };

    });
}
