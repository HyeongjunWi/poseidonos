package arraycmds

import (
	"cli/cmd/displaymgr"
	"cli/cmd/globals"
	"cli/cmd/grpcmgr"
	"fmt"
	pb "kouros/api"

	"github.com/spf13/cobra"
	"google.golang.org/protobuf/encoding/protojson"
)

var AddSpareCmd = &cobra.Command{
	Use:   "addspare [flags]",
	Short: "Add a device as a spare to an array.",
	Long: `
Add a device as a spare to an array. Use this command when you want 
to add a spare device to an array that was created already. 
Please note that the capacity of the spare device must be equal to or greater than the smallest capacity among existing array devices.

Syntax:
	poseidonos-cli array addspare (--spare | -s) DeviceName (--array-name | -a) ArrayName

Example: 
	poseidonos-cli array addspare --spare nvme5 --array-name array0
          `,
	RunE: func(cmd *cobra.Command, args []string) error {

		var command = "ADDDEVICE"

		posMgr, err := grpcmgr.GetPOSManager()
		if err != nil {
			fmt.Printf("failed to connect to POS: %v", err)
			return err
		}

		reqParam, buildErr := buildAddSpareReqParam(command)
		if buildErr != nil {
			fmt.Printf("failed to build request: %v", buildErr)
			return buildErr
		}

		res, req, grpcErr := posMgr.AddSpareDevice(reqParam)

		reqJson, err := protojson.MarshalOptions{
			EmitUnpopulated: true,
		}.Marshal(req)
		if err != nil {
			fmt.Printf("failed to marshal the protobuf request: %v", err)
			return err
		}
		displaymgr.PrintRequest(string(reqJson))

		if grpcErr != nil {
			globals.PrintErrMsg(grpcErr)
			return grpcErr
		}

		printErr := displaymgr.PrintProtoResponse(command, res)
		if printErr != nil {
			fmt.Printf("failed to print the response: %v", printErr)
			return printErr
		}

		return nil
	},
}

func buildAddSpareReqParam(command string) (*pb.AddSpareRequest_Param, error) {
	param := &pb.AddSpareRequest_Param{Array: add_spare_arrayName}
	if add_spare_spareDev != "" {
		param.Spare = append(param.Spare, &pb.AddSpareRequest_SpareDeviceName{DeviceName: add_spare_spareDev})
	}

	return param, nil
}

// Note (mj): In Go-lang, variables are shared among files in a package.
// To remove conflicts between variables in different files of the same package,
// we use the following naming rule: filename_variablename. We can replace this if there is a better way.
var add_spare_arrayName = ""
var add_spare_spareDev = ""

func init() {
	AddSpareCmd.Flags().StringVarP(&add_spare_arrayName,
		"array-name", "a", "",
		"The name of the array to add a spare device.")
	AddSpareCmd.MarkFlagRequired("array-name")

	AddSpareCmd.Flags().StringVarP(&add_spare_spareDev,
		"spare", "s", "",
		"The name of the device to be added to the specified array.")
	AddSpareCmd.MarkFlagRequired("spare")
}
