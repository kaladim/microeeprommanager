from typing import Any, Dict


class ChecksumAlgorithm:
    def __init__(self, params: Dict[str, Any]) -> None:
        assert "algo" in params
        assert isinstance(params["algo"], str)

        if params["algo"].lower() == "crc":
            assert "polynomial" in params
            assert isinstance(params["polynomial"], int)
            assert "initial_value" in params
            assert isinstance(params["initial_value"], int)
            assert "reverse_input" in params
            assert isinstance(params["reverse_input"], bool)
            assert "final_xor_value" in params
            assert isinstance(params["final_xor_value"], int)

            import crcmod

            self.algo = crcmod.mkCrcFun(
                poly=params["polynomial"],
                initCrc=params["initial_value"],
                rev=params["reverse_input"],
                xorOut=params["final_xor_value"],
            )
            self.params: dict = params
        else:
            raise Exception(f"Not implemented checkum algo: {params['algo']}")

    def calculate(self, data: bytes | bytearray) -> int:
        return self.algo(data=data)

    @property
    def parameters(self) -> dict:
        return self.params
